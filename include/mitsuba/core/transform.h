#pragma once

#include <mitsuba/core/ray.h>
#include <enoki/transform.h>
#include <enoki/sphere.h>

NAMESPACE_BEGIN(mitsuba)

/**
 * \brief Encapsulates a 4x4 homogeneous coordinate transformation along with
 * its inverse transpose
 *
 * The Transform class provides a set of overloaded matrix-vector
 * multiplication operators for vectors, points, and normals (all of them
 * behave differently under homogeneous coordinate transformations, hence
 * the need to represent them using separate types)
 */
template <typename Point_> struct Transform {

    // =============================================================
    //! @{ \name Type declarations
    // =============================================================

    static constexpr size_t Size = Point_::Size;

    using Float   = ek::value_t<Point_>;
    using Matrix  = ek::Matrix<Float, Size>;
    using Mask    = ek::mask_t<Float>;
    using Scalar  = ek::scalar_t<Float>;

    //! @}
    // =============================================================

    // =============================================================
    //! @{ \name Fields
    // =============================================================

    Matrix matrix            = ek::identity<Matrix>();
    Matrix inverse_transpose = ek::identity<Matrix>();

    //! @}
    // =============================================================

    // =============================================================
    //! @{ \name Constructors, methods, etc.
    // =============================================================

    /// Initialize the transformation from the given matrix (and compute its inverse transpose)
    Transform(const Matrix &value)
        : matrix(value),
          inverse_transpose(ek::inverse_transpose(value)) { }

    /// Initialize the transformation from the given matrix and its inverse
    Transform(const Matrix &value, const Matrix &inv)
        : matrix(value),
          inverse_transpose(inv) { }

    /// Concatenate transformations
    MTS_INLINE Transform operator*(const Transform &other) const {
        return Transform(matrix * other.matrix,
                         inverse_transpose * other.inverse_transpose);
    }

    /// Compute the inverse of this transformation (involves just shuffles, no arithmetic)
    MTS_INLINE Transform inverse() const {
        return Transform(transpose(inverse_transpose), transpose(matrix));
    }

    /// Get the translation part of a matrix
    Vector<Float, Size - 1> translation() const {
        return ek::head<Size - 1>(matrix.entry(Size - 1));
    }

    /// Equality comparison operator
    bool operator==(const Transform &t) const {
        return matrix == t.matrix &&
               inverse_transpose == t.inverse_transpose;
    }

    /// Inequality comparison operator
    bool operator!=(const Transform &t) const {
        return matrix != t.matrix ||
               inverse_transpose != t.inverse_transpose;
    }

    /**
     * \brief Transform a 3D vector/point/normal/ray by a transformation that
     * is known to be an affine 3D transformation (i.e. no perspective)
     */
    template <typename T>
    MTS_INLINE auto transform_affine(const T &input) const {
        return operator*(input);
    }

    /// Transform a point (handles affine/non-perspective transformations only)
    template <typename T, typename Expr = ek::expr_t<Float, T>>
    MTS_INLINE Point<Expr, Size - 1> transform_affine(const Point<T, Size - 1> &arg) const {
        ek::Array<Expr, Size> result = matrix.entry(Size - 1);

        ENOKI_UNROLL for (size_t i = 0; i < Size - 1; ++i)
            result = ek::fmadd(matrix.entry(i), arg.entry(i), result);

        return ek::head<Size - 1>(result); // no-op
    }

    /**
     * \brief Transform a 3D point
     * \remark In the Python API, this method is named \c transform_point
     */
    template <typename T, typename Expr = ek::expr_t<Float, T>>
    MTS_INLINE Point<Expr, Size - 1> operator*(const Point<T, Size - 1> &arg) const {
        ek::Array<Expr, Size> result = matrix.entry(Size - 1);

        ENOKI_UNROLL for (size_t i = 0; i < Size - 1; ++i)
            result = ek::fmadd(matrix.entry(i), arg.entry(i), result);

        return ek::head<Size - 1>(result) / result.entry(Size - 1);
    }

    /**
     * \brief Transform a 3D vector
     * \remark In the Python API, this method is named \c transform_vector
     */
    template <typename T, typename Expr = ek::expr_t<Float, T>>
    MTS_INLINE Vector<Expr, Size - 1> operator*(const Vector<T, Size - 1> &arg) const {
        ek::Array<Expr, Size> result = matrix.entry(0);
        result *= arg.x();

        ENOKI_UNROLL for (size_t i = 1; i < Size - 1; ++i)
            result = ek::fmadd(matrix.entry(i), arg.entry(i), result);

        return ek::head<Size - 1>(result); // no-op
    }

    /**
     * \brief Transform a 3D normal vector
     * \remark In the Python API, this method is named \c transform_normal
     */
    template <typename T, typename Expr = ek::expr_t<Float, T>>
    MTS_INLINE Normal<Expr, Size - 1> operator*(const Normal<T, Size - 1> &arg) const {
        ek::Array<Expr, Size> result = inverse_transpose.entry(0);
        result *= arg.x();

        ENOKI_UNROLL for (size_t i = 1; i < Size - 1; ++i)
            result = ek::fmadd(inverse_transpose.entry(i), arg.entry(i), result);

        return ek::head<Size - 1>(result); // no-op
    }

    /// Transform a ray (for perspective transformations)
    template <typename T, typename Spectrum, typename Expr = ek::expr_t<Float, T>,
              typename Result = Ray<Point<Expr, Size - 1>, Spectrum>>
    MTS_INLINE Result operator*(const Ray<Point<T, Size - 1>, Spectrum> &ray) const {
        return Result(operator*(ray.o), operator*(ray.d), ray.maxt, ray.time,
                      ray.wavelengths);
    }

    /// Transform a ray (for affine/non-perspective transformations)
    template <typename T, typename Spectrum, typename Expr = ek::expr_t<Float, T>,
              typename Result = Ray<Point<Expr, Size - 1>, Spectrum>>
    MTS_INLINE Result transform_affine(const Ray<Point<T, Size - 1>, Spectrum> &ray) const {
        return Result(transform_affine(ray.o), transform_affine(ray.d),
                      ray.maxt, ray.time, ray.wavelengths);
    }

    /// Create a translation transformation
    static Transform translate(const Vector<Float, Size - 1> &v) {
        return Transform(ek::translate<Matrix>(v),
                         transpose(ek::translate<Matrix>(-v)));
    }

    /// Create a scale transformation
    static Transform scale(const Vector<Float, Size - 1> &v) {
        return Transform(ek::scale<Matrix>(v),
                         // No need to transpose a diagonal matrix.
                         ek::scale<Matrix>(ek::rcp(v)));
    }

    /// Create a rotation transformation around an arbitrary axis in 3D. The angle is specified in degrees
    template <size_t N = Size, ek::enable_if_t<N == 4> = 0>
    static Transform rotate(const Vector<Float, Size - 1> &axis, const Float &angle) {
        Matrix matrix = ek::rotate<Matrix>(axis, ek::deg_to_rad(angle));
        return Transform(matrix, matrix);
    }

    /// Create a rotation transformation in 2D. The angle is specified in degrees
    template <size_t N = Size, ek::enable_if_t<N == 3> = 0>
    static Transform rotate(const Float &angle) {
        Matrix matrix = ek::rotate<Matrix>(ek::deg_to_rad(angle));
        return Transform(matrix, matrix);
    }

    /** \brief Create a perspective transformation.
     *   (Maps [near, far] to [0, 1])
     *
     *  Projects vectors in camera space onto a plane at z=1:
     *
     *  x_proj = x / z
     *  y_proj = y / z
     *  z_proj = (far * (z - near)) / (z * (far-near))
     *
     *  Camera-space depths are not mapped linearly!
     *
     * \param fov Field of view in degrees
     * \param near Near clipping plane
     * \param far  Far clipping plane
     */
    template <size_t N = Size, ek::enable_if_t<N == 4> = 0>
    static Transform perspective(Float fov, Float near_, Float far_) {
        Float recip = 1.f / (far_ - near_);

        /* Perform a scale so that the field of view is mapped
           to the interval [-1, 1] */
        Float tan = ek::tan(ek::deg_to_rad(fov * .5f)),
              cot = 1.f / tan;

        Matrix trafo = ek::diag(Vector<Float, Size>(cot, cot, far_ * recip, 0.f));
        trafo(2, 3) = -near_ * far_ * recip;
        trafo(3, 2) = 1.f;

        Matrix inv_trafo = ek::diag(Vector<Float, Size>(tan, tan, 0.f, ek::rcp(near_)));
        inv_trafo(2, 3) = 1.f;
        inv_trafo(3, 2) = (near_ - far_) / (far_ * near_);

        return Transform(trafo, transpose(inv_trafo));
    }

    /** \brief Create an orthographic transformation, which maps Z to [0,1]
     * and leaves the X and Y coordinates untouched.
     *
     * \param near Near clipping plane
     * \param far  Far clipping plane
     */
    template <size_t N = Size, ek::enable_if_t<N == 4> = 0>
    static Transform orthographic(Float near_, Float far_) {
        return scale({1.f, 1.f, 1.f / (far_ - near_)}) *
               translate({ 0.f, 0.f, -near_ });
    }

    /** \brief Create a look-at camera transformation
     *
     * \param origin Camera position
     * \param target Target vector
     * \param up     Up vector
     */
    template <size_t N = Size, ek::enable_if_t<N == 4> = 0>
    static Transform look_at(const Point<Float, 3> &origin,
                             const Point<Float, 3> &target,
                             const Vector<Float, 3> &up) {
        using Vector1 = ek::Array<Scalar, 1>;
        using Vector3 = Vector<Float, 3>;

        Vector3 dir    = ek::normalize(target - origin);
        Vector3 left   = ek::normalize(ek::cross(up, dir));
        Vector3 new_up = ek::cross(dir, left);

        Vector1 z(0);
        Matrix result = Matrix(
            ek::concat(left, z),
            ek::concat(new_up, z),
            ek::concat(dir, z),
            ek::concat(origin, Vector1(1))
        );

        Matrix inverse = ek::transpose(Matrix(
            ek::concat(left, z),
            ek::concat(new_up, z),
            ek::concat(dir, z),
            Vector<Float, 4>(0.f, 0.f, 0.f, 1.f)
        ));

        inverse[3] = inverse * ek::concat(-origin, Vector1(1));

        return Transform(result, ek::transpose(inverse));
    }

    /// Creates a transformation that converts from the standard basis to 'frame'
    template <typename Value, size_t N = Size, ek::enable_if_t<N == 4> = 0>
    static Transform to_frame(const Frame<Value> &frame) {
        ek::Array<Scalar, 1> z(0);
        Matrix result = Matrix(
            ek::concat(frame.s, z),
            ek::concat(frame.t, z),
            ek::concat(frame.n, z),
            Vector<Float, 4>(0.f, 0.f, 0.f, 1.f)
        );

        return Transform(result, result);
    }

    /// Creates a transformation that converts from 'frame' to the standard basis
    template <typename Value, size_t N = Size, ek::enable_if_t<N == 4> = 0>
    static Transform from_frame(const Frame<Value> &frame) {
        ek::Array<Scalar, 1> z(0);
        Matrix result = ek::transpose(Matrix(
            ek::concat(frame.s, z),
            ek::concat(frame.t, z),
            ek::concat(frame.n, z),
            Vector<Float, 4>(0.f, 0.f, 0.f, 1.f)
        ));

        return Transform(result, result);
    }

    //! @}
    // =============================================================


    // =============================================================
    //! @{ \name Test for transform properties.
    // =============================================================

    /**
     * \brief Test for a scale component in each transform matrix by checking
     * whether <tt>M . M^T == I</tt> (where <tt>M</tt> is the matrix in
     * question and <tt>I</tt> is the identity).
     */
    Mask has_scale() const {
        Mask mask(false);
        for (size_t i = 0; i < Size - 1; ++i) {
            for (size_t j = i; j < Size - 1; ++j) {
                Float sum = 0.f;
                for (size_t k = 0; k < Size - 1; ++k)
                    sum += matrix[i][k] * matrix[j][k];

                mask |= ek::abs(sum - (i == j ? 1.f : 0.f)) > 1e-3f;
            }
        }
        return mask;
    }

    /// Extract a lower-dimensional submatrix
    template <size_t ExtractedSize = Size - 1,
              typename Result = Transform<Point<Float, ExtractedSize>>>
    MTS_INLINE Result extract() const {
        Result result;
        for (size_t i = 0; i < ExtractedSize - 1; ++i) {
            for (size_t j = 0; j < ExtractedSize - 1; ++j) {
                result.matrix.entry(i, j) = matrix.entry(i, j);
                result.inverse_transpose.entry(i, j) =
                    inverse_transpose.entry(i, j);
            }
            result.matrix.entry(ExtractedSize - 1, i) =
                matrix.entry(Size - 1, i);
            result.inverse_transpose.entry(i, ExtractedSize - 1) =
                inverse_transpose.entry(i, Size - 1);
        }

        result.matrix.entry(ExtractedSize - 1, ExtractedSize - 1) =
            matrix.entry(Size - 1, Size - 1);

        result.inverse_transpose.entry(ExtractedSize - 1, ExtractedSize - 1) =
            inverse_transpose.entry(Size - 1, Size - 1);

        return result;
    }

    //! @}
    // =============================================================

    ENOKI_STRUCT(Transform, matrix, inverse_transpose)
};

/**
 * \brief Encapsulates an animated 4x4 homogeneous coordinate transformation
 *
 * The animation is stored as keyframe animation with linear segments. The
 * implementation performs a polar decomposition of each keyframe into a 3x3
 * scale/shear matrix, a rotation quaternion, and a translation vector. These
 * will all be interpolated independently at eval time.
 */
class MTS_EXPORT_CORE AnimatedTransform : public Object {
public:
    using Float = float;
    MTS_IMPORT_CORE_TYPES()

    /// Represents a single keyframe in an animated transform
    struct Keyframe {
        /// Time value associated with this keyframe
        Float time;

        /// 3x3 scale/shear matrix
        Matrix3f scale;

        /// Rotation quaternion
        Quaternion4f quat;

        /// 3D translation
        Vector3f trans;

        Keyframe(const Float time, const Matrix3f &scale,
                 const Quaternion4f &quat, const Vector3f &trans)
            : time(time), scale(scale), quat(quat), trans(trans) { }

        bool operator==(const Keyframe &f) const {
            return (time == f.time && scale == f.scale
                 && quat == f.quat && trans == f.trans);
        }

        bool operator!=(const Keyframe &f) const {
            return !operator==(f);
        }
    };

    /// Create an empty animated transform
    AnimatedTransform() = default;

//     /** Create a constant "animated" transform.
//      * The provided transformation will be used as long as no keyframes
//      * are specified. However, it will be overwritten as soon as the
//      * first keyframe is appended.
//      */
    AnimatedTransform(const Transform4f &trafo)
      : m_transform(trafo) { }

    virtual ~AnimatedTransform();

    /// Append a keyframe to the current animated transform
    void append(Float time, const Transform4f &trafo);

    /// Append a keyframe to the current animated transform
    void append(const Keyframe &keyframe);

    // TODO move this method definition to transform.cpp
    /// Compatibility wrapper, which strips the mask argument and invokes \ref eval()
    template <typename T>
    Transform<Point<T, 4>> eval(T time, ek::mask_t<T> active = true) const {
        using Index        = ek::uint32_array_t<T>;
        using Value        = ek::replace_scalar_t<T, Float>; // ensure we are working with Float32
        using Matrix3f     = ek::Matrix<Value, 3>;
        using Matrix4f     = ek::Matrix<Value, 4>;
        using Quaternion4f = ek::Quaternion<Value>;
        using Vector3f     = Vector<Value, 3>;

        static_assert(!std::is_integral_v<T>,
                      "AnimatedTransform::eval() should be called with a "
                      "floating point-typed `time` parameter");

        ENOKI_MARK_USED(time);
        ENOKI_MARK_USED(active);
        return Transform<Point<T, 4>>(m_transform.matrix);

        // TODO
        // // Perhaps the transformation isn't animated
        // if (likely(size() <= 1))
        //     return Transform<Point<T, 4>>(m_transform.matrix);

        // // Look up the interval containing 'time'
        // Index idx0 = math::find_interval(
        //     (uint32_t) size(),
        //     [&](Index idx) {
        //         constexpr size_t Stride_ = sizeof(Keyframe); // MSVC: have to redeclare constexpr variable in lambda scope :(
        //         return ek::gather<Value, Stride_>(m_keyframes.data(), idx, active) < time;
        //     });

        // Index idx1 = idx0 + 1;

        // // Compute constants describing the layout of the 'Keyframe' data structure
        // constexpr size_t Stride      = sizeof(Keyframe);
        // constexpr size_t ScaleOffset = offsetof(Keyframe, scale) / sizeof(Float);
        // constexpr size_t QuatOffset  = offsetof(Keyframe, quat)  / sizeof(Float);
        // constexpr size_t TransOffset = offsetof(Keyframe, trans) / sizeof(Float);

        // // Compute the relative time value in [0, 1]
        // Value t0 = ek::gather<Value, Stride, false>(m_keyframes.data(), idx0, active),
        //       t1 = ek::gather<Value, Stride, false>(m_keyframes.data(), idx1, active),
        //       t  = ek::min(ek::max((time - t0) / (t1 - t0), 0.f), 1.f);

        // // Interpolate the scale matrix
        // Matrix3f scale0 = ek::gather<Matrix3f, Stride, false>((Float *) m_keyframes.data() + ScaleOffset, idx0, active),
        //          scale1 = ek::gather<Matrix3f, Stride, false>((Float *) m_keyframes.data() + ScaleOffset, idx1, active),
        //          scale  = scale0 * (1 - t) + scale1 * t;

        // // Interpolate the rotation quaternion
        // Quaternion4f quat0 = ek::gather<Quaternion4f, Stride, false>((Float *) m_keyframes.data() + QuatOffset, idx0, active),
        //              quat1 = ek::gather<Quaternion4f, Stride, false>((Float *) m_keyframes.data() + QuatOffset, idx1, active),
        //              quat = ek::slerp(quat0, quat1, t);

        // // Interpolate the translation component
        // Vector3f trans0 = ek::gather<Vector3f, Stride, false>((Float *) m_keyframes.data() + TransOffset, idx0, active),
        //          trans1 = ek::gather<Vector3f, Stride, false>((Float *) m_keyframes.data() + TransOffset, idx1, active),
        //          trans = trans0 * (1 - t) + trans1 * t;

        // return Transform<Point<T, 4>>(
        //     ek::transform_compose<Matrix4f>(scale, quat, trans),
        //     ek::transform_compose_inverse<Matrix4f>(scale, quat, trans)
        // );
    }

    /**
     * \brief Return an axis-aligned box bounding the amount of translation
     * throughout the animation sequence
     */
    BoundingBox3f translation_bounds() const;

    /// Determine whether the transformation involves any kind of scaling
    bool has_scale() const;

    /// Return the number of keyframes
    size_t size() const { return m_keyframes.size(); }

    /// Return a Keyframe data structure
    const Keyframe &operator[](size_t i) const { return m_keyframes[i]; }

    /// Equality comparison operator
    bool operator==(const AnimatedTransform &t) const {
        if (m_transform != t.m_transform ||
            m_keyframes.size() != t.m_keyframes.size()) {
            return false;
        }
        for (size_t i = 0; i < m_keyframes.size(); ++i) {
            if (m_keyframes[i] != t.m_keyframes[i])
                return false;
        }
        return true;
    }

    bool operator!=(const AnimatedTransform &t) const {
        return !operator==(t);
    }

    /// Return a human-readable summary of this bitmap
    virtual std::string to_string() const override;

    MTS_DECLARE_CLASS()
private:
    Transform4f m_transform;
    std::vector<Keyframe> m_keyframes;
};

// -----------------------------------------------------------------------
//! @{ \name Printing
// -----------------------------------------------------------------------

template <typename Point>
std::ostream &operator<<(std::ostream &os, const Transform<Point> &t) {
    os << t.matrix;
    return os;
}

std::ostream &operator<<(std::ostream &os, const AnimatedTransform::Keyframe &frame);

std::ostream &operator<<(std::ostream &os, const AnimatedTransform &t);

//! @}
// -----------------------------------------------------------------------

NAMESPACE_END(mitsuba)
