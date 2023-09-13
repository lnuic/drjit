/*
    apply.cpp -- Implementation of the internal ``apply()``, ``traverse()``,
    and ``transform()`` functions, which recursively perform operations on
    Dr.Jit arrays and Python object trees ("pytrees")

    Dr.Jit: A Just-In-Time-Compiler for Differentiable Rendering
    Copyright 2023, Realistic Graphics Lab, EPFL.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#include "apply.h"
#include "meta.h"
#include "base.h"
#include "memop.h"
#include "shape.h"
#include "init.h"

static const char *op_names[] = {
    // Unary operations
    "__neg__",
    "__invert__",
    "abs",
    "sqrt",
    "rcp",
    "rsqrt",
    "cbrt",

    "exp",
    "exp2",
    "log",
    "log2",

    "sin",
    "cos",
    "sincos",
    "tan",
    "asin",
    "acos",
    "atan",

    "sinh",
    "cosh",
    "sincosh",
    "tanh",
    "asinh",
    "acosh",
    "atanh",

    "erf",

    // Binary arithetic operations
    "__add__",
    "__sub__",
    "__mul__",
    "__truediv__",
    "__floordiv__",
    "__mod__",
    "__lshift__",
    "__rshift__",

    "minimum",
    "maximum",
    "atan2",

    // Binary bit/mask operations
    "__and__",
    "__or__",
    "__xor__",

    // Ternary operations
    "fma",
    "select",

    // Horizontal reductions
    "all",
    "any",

    // Miscellaneous
    "__richcmp__",
};

static_assert(sizeof(op_names) == sizeof(const char *) * (int) ArrayOp::Count,
              "op_names array is missing entries!");

static void raise_incompatible_size_error(Py_ssize_t *sizes, size_t N) {
    std::string msg = "invalid input array sizes (";
    for (size_t i = 0; i < N; ++i) {
        msg += std::to_string(sizes[i]);
        if (i + 2 < N)
            msg += ", ";
        else if (i + 2 == N)
            msg += ", and ";
    }
    msg += ")";
    throw std::runtime_error(msg);
}

/// Forward declaration: specialization of apply() for tensor arguments
template <ApplyMode Mode, typename Func, typename... Args, size_t... Is>
PyObject *apply_tensor(ArrayOp op, Func func, std::index_sequence<Is...>,
                Args... args) noexcept;


/// Alternative to std::max() that also work when only a single argument is given
template <typename... Args> NB_INLINE Py_ssize_t maxv(Py_ssize_t arg, Args... args) {
    if constexpr (sizeof...(Args) > 0) {
        Py_ssize_t other = maxv(args...);
        return other > arg ? other : arg;
    } else {
        return arg;
    }
}

template <typename T1, typename T2> using first_t = T1;
template <typename... T>
nb::handle first(nb::handle h, T&...) { return h; }

template <ApplyMode Mode, typename Slot, typename... Args, size_t... Is>
PyObject *apply(ArrayOp op, Slot slot, std::index_sequence<Is...> is,
                Args... args) noexcept {
    nb::object o[] = { nb::borrow(args)... };
    nb::handle tp = o[0].type();
    constexpr size_t N = sizeof...(Args);

    try {
        // All arguments must first be promoted to the same type
        if (!(o[Is].type().is(tp) && ...)) {
            promote(o, sizeof...(Args), Mode == Select);
            tp = o[Mode == Select ? 1 : 0].type();
        }

        const ArraySupplement &s = supp(tp);
        if (s.is_tensor)
            return apply_tensor<Mode, Slot>(op, slot, is, o[Is].ptr()...);

        void *impl = s[op];

        if (impl == DRJIT_OP_NOT_IMPLEMENTED)
            return nb::not_implemented().release().ptr();

        ArraySupplement::Item item = s.item, item_mask = nullptr;
        ArraySupplement::SetItem set_item;
        nb::handle result_type;

        if constexpr (Mode == RichCompare) {
            raise_if(((s.is_matrix || s.is_complex || s.is_quaternion) &&
                      (slot != Py_EQ && slot != Py_NE)) ||
                         (VarType) s.type == VarType::Pointer,
                     "Inequality comparisons are only permitted on ordinary "
                     "arithmetic arrays. They are suppressed for complex "
                     "arrays, quaternions, matrices, and arrays of pointers.");
            result_type = s.mask;
            set_item = supp(result_type).set_item;
        } else if constexpr (Mode == Select) {
            result_type = tp;
            set_item = s.set_item;
            item_mask = supp(o[0].type()).item;
        } else {
            result_type = tp;
            set_item = s.set_item;
        }
        (void) item_mask;

        drjit::ArrayBase *p[N] = { inst_ptr(o[Is])... };
        nb::object result;

        // In 'InPlace' mode, try to update the 'self' argument when it makes sense
        bool move = Mode == InPlace && o[0].is(first(args...));

        if (impl != DRJIT_OP_DEFAULT) {
            result = nb::inst_alloc(result_type);
            drjit::ArrayBase *pr = inst_ptr(result);

            if constexpr (Mode == RichCompare) {
                using Impl =
                    void (*)(const ArrayBase *, const ArrayBase *, int,
                             ArrayBase *);
                ((Impl) impl)(p[0], p[1], slot, pr);
            } else {
                using Impl = void (*)(first_t<const ArrayBase *, Args>...,
                                      ArrayBase *);
                ((Impl) impl)(p[Is]..., pr);
            }

            nb::inst_mark_ready(result);
        } else {
            /// Initialize an output array of the right size. In 'InPlace'
            /// mode, try to place the output into o[0] if compatible.

            Py_ssize_t l[N], i[N] { }, lr;
            if (s.shape[0] != DRJIT_DYNAMIC) {
                ((l[Is] = s.shape[0]), ...);
                lr = s.shape[0];

                if constexpr (Mode == InPlace) {
                    result = borrow(o[0]);
                    move = false; // can directly construct output into o[0]
                } else {
                    result = nb::inst_alloc_zero(result_type);
                }
            } else {
                ((l[Is] = s.len(p[Is])), ...);
                lr = maxv(l[Is]...);

                if (((l[Is] != lr && l[Is] != 1) || ...))
                    raise_incompatible_size_error(l, N);

                if (Mode == InPlace && lr == l[0]) {
                    result = borrow(o[0]);
                    move = false; // can directly construct output into o[0]
                } else {
                    result = nb::inst_alloc(result_type);
                    s.init(lr, inst_ptr(result));
                    nb::inst_mark_ready(result);
                }
            }

            void *py_impl;
            nb::object py_impl_o;

            // Fetch pointer/handle to function to be applied recursively
            if constexpr (Mode == RichCompare) {
                py_impl = nb::type_get_slot((PyTypeObject *) s.value,
                                            Py_tp_richcompare);
            } else {
                if constexpr (std::is_same_v<Slot, int>)
                    py_impl = nb::type_get_slot((PyTypeObject *) s.value, slot);
                else
                    py_impl_o = array_module.attr(slot);
            }

            for (Py_ssize_t j = 0; j < lr; ++j) {
                // Fetch the j-th element from each array. In 'Select' mode,
                // o[0] is a mask requiring a different accessor function.
                nb::object v[] = { nb::steal(
                    ((Mode == Select && Is == 0) ? item_mask : item)(
                        o[Is].ptr(), i[Is]))... };

                raise_if(!(v[Is].is_valid() && ...), "Item retrival failed!");

                // Recurse
                nb::object vr;
                if constexpr (Mode == RichCompare) {
                    using PyImpl = PyObject *(*)(PyObject *, PyObject *, int);
                    vr = nb::steal(((PyImpl) py_impl)(v[0].ptr(), v[1].ptr(), slot));
                } else {
                    if constexpr (std::is_same_v<Slot, int>) {
                        using PyImpl = PyObject *(*)(first_t<PyObject *, Args>...);
                        vr = nb::steal(((PyImpl) py_impl)(v[Is].ptr()...));
                    } else {
                        vr = py_impl_o(v[Is]...);
                    }
                }

                raise_if(!vr.is_valid(), "Nested operation failed!");

                // Assign result
                raise_if(set_item(result.ptr(), j, vr.ptr()),
                         "Item assignment failed!");

                // Advance to next element, broadcast size-1 arrays
                ((i[Is] += (l[Is] == 1 ? 0 : 1)), ...);
            }
        }

        // In in-place mode, if a separate result object had to be
        // constructed, use it to now replace the contents of o[0]
        if (move) {
            nb::inst_replace_move(o[0], result);
            result = borrow(o[0]);
        }

        return result.release().ptr();
    } catch (nb::python_error &e) {
        nb::str tp_name = nb::type_name(tp);
        e.restore();
        if constexpr (std::is_same_v<Slot, const char *>)
            nb::chain_error(PyExc_RuntimeError, "drjit.%s(<%U>): failed (see above)!",
                            op_names[(int) op], tp_name.ptr());
        else
            nb::chain_error(PyExc_RuntimeError, "%U.%s(): failed (see above)!",
                            tp_name.ptr(), op_names[(int) op]);
    } catch (const std::exception &e) {
        nb::str tp_name = nb::type_name(tp);
        if constexpr (std::is_same_v<Slot, const char *>)
            nb::chain_error(PyExc_RuntimeError, "drjit.%s(<%U>): %s",
                            op_names[(int) op], tp_name.ptr(), e.what());
        else
            nb::chain_error(PyExc_RuntimeError, "%U.%s(): %s", tp_name.ptr(),
                            op_names[(int) op], e.what());
    }

    return nullptr;
}

void tensor_broadcast(nb::object &tensor, nb::object &array,
                      const dr_vector<size_t> &shape_src,
                      const dr_vector<size_t> &shape_dst) {
    size_t ndim = shape_src.size();
    if (ndim == 0 || memcmp(shape_src.data(), shape_dst.data(), sizeof(size_t) * ndim) == 0)
        return;

    // At this point, we know that shape_src.size() == shape_dst.size()
    // See apply_tensor for details.

    size_t size = 1;
    for (size_t i = 0; i < ndim; ++i)
        size *= shape_dst[i];

    nb::handle tp = tensor.type();
    const ArraySupplement &s = supp(tp);

    nb::type_object_t<ArrayBase> index_type =
        nb::borrow<nb::type_object_t<ArrayBase>>(s.tensor_index);

    nb::object index  = arange(index_type, 0, (Py_ssize_t) size, 1),
               size_o = index_type(size);

    for (size_t i = 0; i < ndim; ++i) {
        size_t size_next = size / shape_dst[i];

        nb::object size_next_o = index_type(size_next);

        if (shape_src[i] == 1 && shape_dst[i] != 1)
            index = (index % size_next_o) + index.floor_div(size_o) * size_next_o;

        size = size_next;
        size_o = std::move(size_next_o);
    }

    array = gather(nb::borrow<nb::type_object>(array.type()),
                   array, index, nb::borrow(Py_True));
}


template <ApplyMode Mode, typename Slot, typename... Args, size_t... Is>
NB_NOINLINE PyObject *apply_tensor(ArrayOp op, Slot slot,
                                   std::index_sequence<Is...> is,
                                   Args... args) noexcept {

    nb::object o[] = { nb::borrow(args)... };
    nb::handle tp = o[0].type();

    try {
        constexpr size_t N = sizeof...(Args);

        // All arguments must first be promoted to the same type
        if (!(o[Is].type().is(tp) && ...)) {
            promote(o, sizeof...(Args), Mode == Select);
            tp = o[Mode == Select ? 1 : 0].type();
        }

        // In 'InPlace' mode, try to update the 'self' argument when it makes sense
        bool move = Mode == InPlace && o[0].is(first(args...));

        const ArraySupplement *s[] = { &supp(o[Is].type())... };

        nb::object arrays[] = {
            nb::steal(s[Is]->tensor_array(o[Is].ptr()))...
        };

        const dr_vector<size_t> *shapes[N] = {
            &s[Is]->tensor_shape(inst_ptr(o[Is]))...
        };

        size_t ndims[] = { shapes[Is]->size()... };
        size_t ndim = maxv(ndims[Is]...);
        bool compatible = true;

        if constexpr (sizeof...(Is) > 1) {
            if (((ndim != ndims[Is] && ndims[Is] != 0) || ...))
                compatible = false;
        }

        dr_vector<size_t> shape(ndim, 0);

        if (compatible) {
            for (size_t i = 0; i < ndim; ++i) {
                size_t shape_i[] = { (ndims[Is] ? shapes[Is]->operator[](i) : 1)... };
                size_t value = maxv(shape_i[Is]...);
                if (((shape_i[Is] != value && shape_i[Is] != 1 && ndims[Is]) || ...)) {
                    compatible = false;
                    break;
                }
                shape[i] = value;
            }
        }

        if (!compatible) {
            nb::str shape_str[] = { nb::str(cast_shape(*shapes[Is]))... };

            const char *fmt = N == 2
                ? "Operands have incompatible shapes: %s and %s."
                : "Operands have incompatible shapes: %s, %s, and %s.";

            nb::detail::raise(fmt, shape_str[Is].c_str()...);
        }

        if constexpr (N > 1) {
            // Broadcast to compatible shape for binary/ternary operations
            (tensor_broadcast(o[Is], arrays[Is], *shapes[Is], shape), ...);
        }

        constexpr ApplyMode NestedMode = Mode == InPlace ? Normal : Mode;

        nb::object result_array = nb::steal(apply<NestedMode, Slot>(
            op, slot, is, arrays[Is].ptr()...));

        raise_if(!result_array.is_valid(),
                 "Operation on underlying array failed.");

        nb::handle result_type = tp;
        if (Mode == RichCompare)
            result_type = s[0]->mask;

        nb::object result = result_type(result_array, cast_shape(shape));

        if (move) {
            nb::inst_replace_move(o[0], result);
            result = borrow(o[0]);
        }

        return result.release().ptr();
    } catch (nb::python_error &e) {
        nb::str tp_name = nb::type_name(tp);
        e.restore();
        if constexpr (std::is_same_v<Slot, const char *>)
            nb::chain_error(PyExc_RuntimeError, "drjit.%s(<%U>): failed (see above).",
                            op_names[(int) op], tp_name.ptr());
        else
            nb::chain_error(PyExc_RuntimeError, "%U.%s(): failed (see above).",
                            tp_name.ptr(), op_names[(int) op]);
    } catch (const std::exception &e) {
        nb::str tp_name = nb::type_name(tp);
        if constexpr (std::is_same_v<Slot, const char *>)
            nb::chain_error(PyExc_RuntimeError, "drjit.%s(<%U>): %s",
                            op_names[(int) op], tp_name.ptr(), e.what());
        else
            nb::chain_error(PyExc_RuntimeError, "%U.%s(): %s", tp_name.ptr(),
                            op_names[(int) op], e.what());
    }
    return nullptr;
}


void traverse(const char *op, const TraverseCallback &tc, nb::handle h) {
    nb::handle tp = h.type();

    try {
        if (is_drjit_type(tp)) {
            const ArraySupplement &s = supp(tp);
            if (s.is_tensor) {
                tc(nb::steal(s.tensor_array(h.ptr())));
            } else if (s.ndim > 1) {
                Py_ssize_t len = s.shape[0];
                if (len == DRJIT_DYNAMIC)
                    len = s.len(inst_ptr(h));

                for (Py_ssize_t i = 0; i < len; ++i)
                    traverse(op, tc, nb::steal(s.item(h.ptr(), i)));
            } else  {
                tc(h);
            }
        } else if (tp.is(&PyTuple_Type)) {
            for (nb::handle h2 : nb::borrow<nb::tuple>(h))
                traverse(op, tc, h2);
        } else if (tp.is(&PyList_Type)) {
            for (nb::handle h2 : nb::borrow<nb::list>(h))
                traverse(op, tc, h2);
        } else if (tp.is(&PyDict_Type)) {
            for (nb::handle h2 : nb::borrow<nb::dict>(h).values())
                traverse(op, tc, h2);
        } else {
            nb::object dstruct = nb::getattr(tp, "DRJIT_STRUCT", nb::handle());
            if (dstruct.is_valid() && dstruct.type().is(&PyDict_Type)) {
                for (auto [k, v] : nb::borrow<nb::dict>(dstruct))
                    traverse(op, tc, nb::getattr(h, k));
            }
        }
    } catch (nb::python_error &e) {
        nb::str tp_name = nb::type_name(tp);
        e.restore();
        nb::chain_error(PyExc_RuntimeError,
                        "%s(): error encountered while processing an argument "
                        "of type '%U' (see above).", op, tp_name.ptr());
        throw nb::python_error();
    } catch (const std::exception &e) {
        nb::str tp_name = nb::type_name(tp);
        nb::chain_error(PyExc_RuntimeError,
                        "%s(): error encountered while processing an argument "
                        "of type '%U': %s", op, tp_name.ptr(), e.what());
        throw nb::python_error();
    }
}

void traverse_pair(const char *op, const TraversePairCallback &tc,
                   nb::handle h1, nb::handle h2) {
    nb::handle tp1 = h1.type(),
               tp2 = h2.type();

    try {
        if (!tp1.is(tp2))
            nb::detail::raise("Mismatched input types.");

        if (is_drjit_type(tp1)) {
            const ArraySupplement &s = supp(tp1);

            if (s.is_tensor) {
                tc(nb::steal(s.tensor_array(h1.ptr())),
                   nb::steal(s.tensor_array(h2.ptr())));
            } else if (s.ndim > 1) {
                Py_ssize_t len1 = s.shape[0], len2 = len1;
                if (len1 == DRJIT_DYNAMIC) {
                    len1 = s.len(inst_ptr(h1));
                    len2 = s.len(inst_ptr(h2));
                }

                if (len1 != len2)
                    nb::detail::raise("Incompatible input lengths (%zu and %zu).", len1, len2);

                for (Py_ssize_t i = 0; i < len1; ++i)
                    traverse_pair(op, tc,
                                  nb::steal(s.item(h1.ptr(), i)),
                                  nb::steal(s.item(h2.ptr(), i)));
            } else  {
                tc(h1, h2);
            }

            return;
        }

        if (tp1.is(&PyTuple_Type) || tp1.is(&PyList_Type)) {
            size_t len1 = nb::len(h1),
                   len2 = nb::len(h2);
            if (len1 != len2)
                nb::detail::raise("Incompatible input lengths (%zu and %zu).", len1, len2);
            for (size_t i = 0; i < len1; ++i)
                traverse_pair(op, tc, h1[i], h2[i]);
        } else if (tp1.is(&PyDict_Type)) {
            nb::dict d1 = nb::borrow<nb::dict>(h1),
                     d2 = nb::borrow<nb::dict>(h2);
            nb::object k1 = d1.keys(), k2 = d2.keys();
            if (!k1.equal(k2))
                nb::detail::raise("Dictionaries have mismatched keys.");
            for (nb::handle k : k1)
                traverse_pair(op, tc, d1[k], d2[k]);
        } else {
            nb::object dstruct = nb::getattr(tp1, "DRJIT_STRUCT", nb::handle());
            if (dstruct.is_valid() && dstruct.type().is(&PyDict_Type)) {
                for (auto [k, v] : nb::borrow<nb::dict>(dstruct))
                    traverse_pair(op, tc,
                                  nb::getattr(h1, k),
                                  nb::getattr(h2, k));
            }
        }
    } catch (nb::python_error &e) {
        nb::str tp_name_1 = nb::type_name(tp1),
                tp_name_2 = nb::type_name(tp2);
        e.restore();
        nb::chain_error(PyExc_RuntimeError,
                        "%s(): error encountered while processing arguments "
                        "of type '%U' and '%U' (see above).",
                        op, tp_name_1.ptr(), tp_name_2.ptr());
        throw nb::python_error();
    } catch (const std::exception &e) {
        nb::str tp_name_1 = nb::type_name(tp1),
                tp_name_2 = nb::type_name(tp2);
        nb::chain_error(PyExc_RuntimeError,
                        "%s(): error encountered while processing arguments "
                        "of type '%U' and '%U': %s",
                        op, tp_name_1.ptr(), tp_name_2.ptr(), e.what());
        throw nb::python_error();
    }
}

nb::handle TransformCallback::transform_type(nb::handle tp) const {
    return tp;
}

nb::object transform(const char *op, const TransformCallback &tc, nb::handle h1) {
    nb::handle t1 = h1.type();

    try {
        if (is_drjit_type(t1)) {
            nb::handle t2 = tc.transform_type(t1);
            if (!t2.is_valid())
                return nb::none();

            const ArraySupplement &s1 = supp(t1),
                                  &s2 = supp(t2);

            nb::object h2 = nb::inst_alloc_zero(t2);
            if (s1.is_tensor) {
                tc(nb::steal(s1.tensor_array(h1.ptr())),
                   nb::steal(s2.tensor_array(h2.ptr())));
            } else if (s1.ndim != 1) {
                dr::ArrayBase *p1 = inst_ptr(h1),
                              *p2 = inst_ptr(h2);

                size_t size = s1.shape[0];
                if (size == DRJIT_DYNAMIC) {
                    size = s1.len(p1);
                    s2.init(size, p2);
                }

                for (size_t i = 0; i < size; ++i) {
                    nb::object o = h1[i];
                    h2[i] = transform(op, tc, o);
                }
            } else {
                tc(h1, h2);
            }
            return h2;
        } else if (t1.is(&PyTuple_Type)) {
            nb::tuple t = nb::borrow<nb::tuple>(h1);
            size_t size = nb::len(t);
            nb::object result = nb::steal(PyTuple_New(size));
            for (size_t i = 0; i < size; ++i)
                PyTuple_SET_ITEM(result.ptr(), i,
                                 transform(op, tc, t[i]).release().ptr());
            return result;
        } else if (t1.is(&PyList_Type)) {
            nb::list result;
            for (nb::handle item : nb::borrow<nb::list>(h1))
                result.append(transform(op, tc, item));
            return result;
        } else if (t1.is(&PyDict_Type)) {
            nb::dict result;
            for (auto [k, v] : nb::borrow<nb::dict>(h1))
                result[k] = transform(op, tc, v);
            return result;
        } else {
            nb::object dstruct = nb::getattr(t1, "DRJIT_STRUCT", nb::handle());
            if (dstruct.is_valid() && dstruct.type().is(&PyDict_Type)) {
                nb::object result = t1();
                for (auto [k, v] : nb::borrow<nb::dict>(dstruct))
                    nb::setattr(result, k, transform(op, tc, nb::getattr(h1, k)));
                return result;
            } else {
                return nb::borrow(h1);
            }
        }
    } catch (nb::python_error &e) {
        nb::str tp_name = nb::type_name(t1);
        e.restore();
        nb::chain_error(PyExc_RuntimeError,
                        "%s(): error encountered while processing an argument "
                        "of type '%U' (see above).", op, tp_name.ptr());
        throw nb::python_error();
    } catch (const std::exception &e) {
        nb::str tp_name = nb::type_name(t1);
        nb::chain_error(PyExc_RuntimeError,
                        "%s(): error encountered while processing an argument "
                        "of type '%U': %s", op, tp_name.ptr(), e.what());
        throw nb::python_error();
    }
}


template PyObject *apply<Normal>(ArrayOp, int, std::index_sequence<0>,
                                 PyObject *) noexcept;
template PyObject *apply<Normal>(ArrayOp, int, std::index_sequence<0, 1>,
                                 PyObject *, PyObject *) noexcept;
template PyObject *apply<Normal>(ArrayOp, int, std::index_sequence<0, 1, 2>,
                                 PyObject *, PyObject *, PyObject *) noexcept;
template PyObject *apply<Normal>(ArrayOp, const char *, std::index_sequence<0>,
                                 PyObject *) noexcept;
template PyObject *apply<Normal>(ArrayOp, const char *, std::index_sequence<0, 1>,
                                 PyObject *, PyObject *) noexcept;
template PyObject *apply<Normal>(ArrayOp, const char *, std::index_sequence<0, 1, 2>,
                                 PyObject *, PyObject *, PyObject *) noexcept;
template PyObject *apply<Select>(ArrayOp, const char *, std::index_sequence<0, 1, 2>,
                                 PyObject *, PyObject *, PyObject *) noexcept;
template PyObject *apply<RichCompare>(ArrayOp, int, std::index_sequence<0, 1>,
                                      PyObject *, PyObject *) noexcept;
template PyObject *apply<InPlace>(ArrayOp, int, std::index_sequence<0, 1>,
                                  PyObject *, PyObject *) noexcept;
