///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
///
/// @file RayTracer.h
///
/// @author Ken Museth
///
/// @brief Defines two simple but multithreaded renders, a level-set
/// ray tracer and a volume render. To support these renders we also define
/// perspective and orthographic cameras (both designed to mimic a Houdini camera),
/// a Film class and some rather naive shaders.
///
/// @note These classes are included mainly as reference implementations for
/// ray-tracing of OpenVDB volumes. In other words they are not intended for 
/// production-quality rendering, but could be used for fast pre-visualiztion 
/// or as a startingpoint for a more serious render.

#ifndef OPENVDB_TOOLS_RAYTRACER_HAS_BEEN_INCLUDED
#define OPENVDB_TOOLS_RAYTRACER_HAS_BEEN_INCLUDED

#include <openvdb/Types.h>
#include <openvdb/math/BBox.h>
#include <openvdb/math/Ray.h>
#include <openvdb/math/Math.h>
#include <openvdb/tools/RayIntersector.h>
#include <openvdb/tools/Interpolation.h>
#include <boost/scoped_ptr.hpp>
#include <vector>

#ifdef OPENVDB_TOOLS_RAYTRACER_USE_EXR
#include <OpenEXR/ImfPixelType.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfFrameBuffer.h>
#endif

namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace tools {

// Forward declarations
class BaseCamera;
class BaseShader;

/// @brief Ray-trace a volume.
template<typename GridT>
inline void rayTrace(const GridT&,
                     const BaseShader&,
                     BaseCamera&,
                     size_t pixelSamples = 1,
                     unsigned int seed = 0,
                     bool threaded = true);

/// @brief Ray-trace a volume using a given ray intersector.
template<typename GridT, typename IntersectorT>
inline void rayTrace(const GridT&,
                     const IntersectorT&,
                     const BaseShader&,
                     BaseCamera&,
                     size_t pixelSamples = 1,
                     unsigned int seed = 0,
                     bool threaded = true);


///////////////////////////////LEVEL SET RAY TRACER ///////////////////////////////////////

/// @brief A (very) simple multithreaded ray tracer specifically for narrow-band level sets.
/// @details Included primarily as a reference implementation.
template<typename GridT, typename IntersectorT = tools::LevelSetRayIntersector<GridT> >
class LevelSetRayTracer
{
public:
    typedef GridT                           GridType;
    typedef typename IntersectorT::Vec3Type Vec3Type;
    typedef typename IntersectorT::RayType  RayType;

    /// @brief Constructor based on an instance of the grid to be rendered.
    LevelSetRayTracer(const GridT& grid,
                      const BaseShader& shader,
                      BaseCamera& camera,
                      size_t pixelSamples = 1,
                      unsigned int seed = 0);

    /// @brief Constructor based on an instance of the intersector
    /// performing the ray-intersections.
    LevelSetRayTracer(const IntersectorT& inter,
                      const BaseShader& shader,
                      BaseCamera& camera,
                      size_t pixelSamples = 1,
                      unsigned int seed = 0);

    /// @brief Copy constructor
    LevelSetRayTracer(const LevelSetRayTracer& other);

    /// @brief Destructor
    ~LevelSetRayTracer();
    
    /// @brief Set the level set grid to be ray-traced
    void setGrid(const GridT& grid);

    /// @brief Set the intersector that performs the actual
    /// intersection of the rays against the narrow-band level set.
    void setIntersector(const IntersectorT& inter);

    /// @brief Set the shader derived from the abstract BaseShader class.
    ///
    /// @note The shader is not assumed to be thread-safe so each
    /// thread will get it's only deep copy. For instance it could
    /// contains a ValueAccessor into another grid with auxiliary
    /// shading information. Thus, make sure it is relatively 
    /// light-weight and efficient to copy (which is the case for ValueAccesors).
    void setShader(const BaseShader& shader);

    /// @brief Set the camera derived from the abstract BaseCamera class.
    void setCamera(BaseCamera& camera);
    
    /// @brief Set the number of pixel samples and the seed for
    /// jittered sub-rays. A value larger then one implies
    /// anti-aliasing by jittered super-sampling.
    /// @throw ValueError if pixelSamples is equal to zero.
    void setPixelSamples(size_t pixelSamples, unsigned int seed = 0);

    /// @brief Perform the actual (potentially multithreaded) ray-tracing.
    void render(bool threaded = true) const;

    /// @brief Public method required by tbb::parallel_for.
    /// @warning Never call it directly.
    void operator()(const tbb::blocked_range<size_t>& range) const;

private:
    const bool                          mIsMaster;
    double*                             mRand;
    IntersectorT                        mInter;
    boost::scoped_ptr<const BaseShader> mShader;
    BaseCamera*                         mCamera;
    size_t                              mSubPixels;
};// LevelSetRayTracer


///////////////////////////////VOLUME RENDER ///////////////////////////////////////

/// @brief A (very) simple multithreaded volume render specifically for scalar density.
/// @details Included primarily as a reference implementation.
/// @note It will only compile if the IntersectorT is templated on a Grid with a 
/// floating-point voxel type.    
template <typename IntersectorT, typename SamplerT = tools::BoxSampler>
class VolumeRender
{
public:

    typedef typename IntersectorT::GridType GridType;
    typedef typename IntersectorT::RayType  RayType;
    typedef typename GridType::ValueType    ValueType;
    typedef tools::GridSampler<typename GridType::ConstAccessor, SamplerT> SamplerType;
    BOOST_STATIC_ASSERT(boost::is_floating_point<ValueType>::value);
    
    VolumeRender(const IntersectorT& inter, BaseCamera& camera);

    /// @brief Perform the actual (potentially multithreaded) volume rendering.
    void render(bool threaded=true) const;

    /// @brief Set the camera derived from the abstract BaseCamera class.
    void setCamera(BaseCamera& camera) { mCamera = &camera; }

    /// @brief Set the intersector that performs the actual
    /// intersection of the rays against the volume.
    void setIntersector(const IntersectorT& inter) { mIntersector = &inter; }
    
    /// @brief Set the vector components of a directional light source
    /// @throw ArithmeticError if input is a null vector.
    void setLightDir(Real x, Real y, Real z) { mLightDir = Vec3R(x,y,z).unit(); }

    /// @brief Set the color of the direcitonal light source.
    void setLightColor(Real r, Real g, Real b) { mLightColor = Vec3R(r,g,b); }

    /// @brief Set the integration step-size in voxel units for the primay ray.
    void setPrimaryStep(Real primaryStep) { mPrimaryStep = primaryStep; }

    /// @brief Set the integration step-size in voxel units for the primay ray.
    void setShadowStep(Real shadowStep) { mShadowStep  = shadowStep; }

    /// @brief Set Scattering coefficients.
    void setScattering(Real x, Real y, Real z) { mScattering = Vec3R(x,y,z); }

    /// @brief Set absorption coefficients.
    void setAbsorption(Real x, Real y, Real z) { mAbsorption = Vec3R(x,y,z); }

    /// @brief Set parameter that imitates multi-scattering. A value
    /// of zero implies no multi-scattering. 
    void setLightGain(Real gain) { mLightGain = gain; }

    /// @brief Set the cut-off value for density and transmittance.
    void setCutOff(Real cutOff) { mCutOff = cutOff; }

    /// @brief Public method required by tbb::parallel_for.
    /// @warning Never call it directly.
    void operator()(const tbb::blocked_range<size_t>& range) const;
    
private:
    
    BaseCamera* mCamera;
    const IntersectorT* mIntersector;

    Real  mPrimaryStep, mShadowStep, mCutOff, mLightGain;
    Vec3R mLightDir, mLightColor, mAbsorption, mScattering;
};//VolumeRender    

//////////////////////////////////////// FILM ////////////////////////////////////////

/// @brief A simple class that allows for concurrent writes to pixels in an image,
/// background initialization of the image, and PPM or EXR file output.
class Film
{
public:
    /// @brief Floating-point RGBA components in the range [0, 1].
    /// @details This is our preferred representation for color processing.
    struct RGBA
    {
        typedef float ValueT;

        RGBA() : r(0), g(0), b(0), a(1) {}
        explicit RGBA(ValueT intensity) : r(intensity), g(intensity), b(intensity), a(1) {}
        RGBA(ValueT _r, ValueT _g, ValueT _b, ValueT _a=1.0f) : r(_r), g(_g), b(_b), a(_a) {}

        RGBA  operator* (ValueT scale)  const { return RGBA(r*scale, g*scale, b*scale);}
        RGBA  operator+ (const RGBA& rhs) const { return RGBA(r+rhs.r, g+rhs.g, b+rhs.b);}
        RGBA  operator* (const RGBA& rhs) const { return RGBA(r*rhs.r, g*rhs.g, b*rhs.b);}
        RGBA& operator+=(const RGBA& rhs) { r+=rhs.r; g+=rhs.g; b+=rhs.b, a+=rhs.a; return *this;}

        void over(const RGBA& rhs)
        {
            const float s = rhs.a*(1.0f-a);
            r = a*r+s*rhs.r;
            g = a*g+s*rhs.g;
            b = a*b+s*rhs.b;
            a = a + s;
        }

        ValueT r, g, b, a;
    };


    Film(size_t width, size_t height)
        : mWidth(width), mHeight(height), mSize(width*height), mPixels(new RGBA[mSize])
    {
    }
    Film(size_t width, size_t height, const RGBA& bg)
        : mWidth(width), mHeight(height), mSize(width*height), mPixels(new RGBA[mSize])
    {
        this->fill(bg);
    }
    ~Film() { delete mPixels; }

    const RGBA& pixel(size_t w, size_t h) const
    {
        assert(w < mWidth);
        assert(h < mHeight);
        return mPixels[w + h*mWidth];
    }

    RGBA& pixel(size_t w, size_t h)
    {
        assert(w < mWidth);
        assert(h < mHeight);
        return mPixels[w + h*mWidth];
    }

    void fill(const RGBA& rgb=RGBA(0)) { for (size_t i=0; i<mSize; ++i) mPixels[i] = rgb; }
    void checkerboard(const RGBA& c1=RGBA(0.3f), const RGBA& c2=RGBA(0.6f), size_t size=32)
    {
        RGBA *p = mPixels;
        for (size_t j = 0; j < mHeight; ++j) {
            for (size_t i = 0; i < mWidth; ++i, ++p) {
                *p = ((i & size) ^ (j & size)) ? c1 : c2;
            }
        }
    }

    void savePPM(const std::string& fileName)
    {
        std::string name(fileName + ".ppm");
        unsigned char* tmp = new unsigned char[3*mSize], *q = tmp;
        RGBA* p = mPixels;
        size_t n = mSize;
        while (n--) {
            *q++ = static_cast<unsigned char>(255.0f*(*p  ).r);
            *q++ = static_cast<unsigned char>(255.0f*(*p  ).g);
            *q++ = static_cast<unsigned char>(255.0f*(*p++).b);
        }

        std::ofstream os(name.c_str(), std::ios_base::binary);
        if (!os.is_open()) {
            std::cerr << "Error opening PPM file \"" << name << "\"" << std::endl;
            return;
        }

        os << "P6\n" << mWidth << " " << mHeight << "\n255\n";
        os.write((const char *)&(*tmp), 3*mSize*sizeof(unsigned char));
        delete [] tmp;
    }

#ifdef OPENVDB_TOOLS_RAYTRACER_USE_EXR
    void saveEXR(const std::string& fileName, size_t compression = 2, size_t threads = 8)
    {
        std::string name(fileName + ".exr");

        if (threads>0) Imf::setGlobalThreadCount(threads);
        Imf::Header header(mWidth, mHeight);
        if (compression==0) header.compression() = Imf::NO_COMPRESSION;
        if (compression==1) header.compression() = Imf::RLE_COMPRESSION;
        if (compression>=2) header.compression() = Imf::ZIP_COMPRESSION;
        header.channels().insert("R", Imf::Channel(Imf::FLOAT));
        header.channels().insert("G", Imf::Channel(Imf::FLOAT));
        header.channels().insert("B", Imf::Channel(Imf::FLOAT));
        header.channels().insert("A", Imf::Channel(Imf::FLOAT));

        Imf::FrameBuffer framebuffer;
        framebuffer.insert("R", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].r),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        framebuffer.insert("G", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].g),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        framebuffer.insert("B", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].b),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));
        framebuffer.insert("A", Imf::Slice( Imf::FLOAT, (char *) &(mPixels[0].a),
                                            sizeof (RGBA), sizeof (RGBA) * mWidth));

        Imf::OutputFile file(name.c_str(), header);
        file.setFrameBuffer(framebuffer);
        file.writePixels(mHeight);
    }
#endif

    size_t width()       const { return mWidth; }
    size_t height()      const { return mHeight; }
    size_t numPixels()   const { return mSize; }
    const RGBA* pixels() const { return mPixels; }

private:
    size_t mWidth, mHeight, mSize;
    RGBA*  mPixels;
};// Film


//////////////////////////////////////// CAMERAS ////////////////////////////////////////

/// Abstract base class for the perspective and orthographic cameras
class BaseCamera
{
public:
    BaseCamera(Film& film, const Vec3R& rotation, const Vec3R& translation,
               double frameWidth, double nearPlane, double farPlane)
        : mFilm(&film)
        , mScaleWidth(frameWidth)
        , mScaleHeight(frameWidth*film.height()/double(film.width()))
    {
        assert(nearPlane > 0 && farPlane > nearPlane);
        mScreenToWorld.accumPostRotation(math::X_AXIS, rotation[0] * M_PI / 180.0);
        mScreenToWorld.accumPostRotation(math::Y_AXIS, rotation[1] * M_PI / 180.0);
        mScreenToWorld.accumPostRotation(math::Z_AXIS, rotation[2] * M_PI / 180.0);
        mScreenToWorld.accumPostTranslation(translation);
        this->initRay(nearPlane, farPlane);
    }

    virtual ~BaseCamera() {}

    Film::RGBA& pixel(size_t i, size_t j) { return mFilm->pixel(i, j); }

    size_t width()  const { return mFilm->width(); }
    size_t height() const { return mFilm->height(); }

    /// Rotate the camera so its negative z-axis points at xyz and its
    /// y axis is in the plane of the xyz and up vectors. In other
    /// words the camera will look at xyz and use up as the
    /// horizontal direction.
    void lookAt(const Vec3R& xyz, const Vec3R& up = Vec3R(0.0, 1.0, 0.0))
    {
        const Vec3R orig = mScreenToWorld.applyMap(Vec3R(0.0));
        const Vec3R dir  = orig - xyz;
        try {
            Mat4d xform = math::aim<Mat4d>(dir, up);
            xform.postTranslate(orig);
            mScreenToWorld = math::AffineMap(xform);
            this->initRay(mRay.t0(), mRay.t1());
        } catch (...) {}
    }

    Vec3R rasterToScreen(double i, double j, double z) const
    {
        return Vec3R( (2 * i / mFilm->width() - 1)  * mScaleWidth,
                      (1 - 2 * j / mFilm->height()) * mScaleHeight, z );
    }

    /// @brief Return a Ray in world space given the pixel indices and
    /// optional offsets in the range [0, 1]. An offset of 0.5 corresponds
    /// to the center of the pixel.
    virtual math::Ray<double> getRay(
        size_t i, size_t j, double iOffset = 0.5, double jOffset = 0.5) const = 0;

protected:
    void initRay(double t0, double t1)
    {
        mRay.setTimes(t0, t1);
        mRay.setEye(mScreenToWorld.applyMap(Vec3R(0.0)));
        mRay.setDir(mScreenToWorld.applyJacobian(Vec3R(0.0, 0.0, -1.0)));
    }

    Film* mFilm;
    double mScaleWidth, mScaleHeight;
    math::Ray<double> mRay;
    math::AffineMap mScreenToWorld;
};// BaseCamera


class PerspectiveCamera: public BaseCamera
{
  public:
    /// @brief Constructor
    /// @param film         film (i.e. image) defining the pixel resolution
    /// @param rotation     rotation in degrees of the camera in world space
    ///                     (applied in x, y, z order)
    /// @param translation  translation of the camera in world-space units,
    ///                     applied after rotation
    /// @param focalLength  focal length of the camera in mm
    ///                     (the default of 50mm corresponds to Houdini's default camera)
    /// @param aperture     width in mm of the frame, i.e., the visible field
    ///                     (the default 41.2136 mm corresponds to Houdini's default camera)
    /// @param nearPlane    depth of the near clipping plane in world-space units
    /// @param farPlane     depth of the far clipping plane in world-space units
    ///
    /// @details If no rotation or translation is provided, the camera is placed
    /// at (0,0,0) in world space and points in the direction of the negative z axis.
    PerspectiveCamera(Film& film,
                      const Vec3R& rotation    = Vec3R(0.0),
                      const Vec3R& translation = Vec3R(0.0),
                      double focalLength = 50.0,
                      double aperture    = 41.2136,
                      double nearPlane   = 1e-3,
                      double farPlane    = std::numeric_limits<double>::max())
        : BaseCamera(film, rotation, translation, 0.5*aperture/focalLength, nearPlane, farPlane)
    {
    }

    virtual ~PerspectiveCamera() {}

    /// @brief Return a Ray in world space given the pixel indices and
    /// optional offsets in the range [0,1]. An offset of 0.5 corresponds
    /// to the center of the pixel.
    virtual math::Ray<double> getRay(
        size_t i, size_t j, double iOffset = 0.5, double jOffset = 0.5) const
    {
        math::Ray<double> ray(mRay);
        Vec3R dir = BaseCamera::rasterToScreen(i + iOffset, j + jOffset, -1.0);
        dir = BaseCamera::mScreenToWorld.applyJacobian(dir);
        dir.normalize();
        ray.scaleTimes(1.0/dir.dot(ray.dir()));
        ray.setDir(dir);
        return ray;
    }

    /// @brief Return the horizontal field of view in degrees given a
    /// focal lenth in mm and the specified aperture in mm.
    static double focalLengthToFieldOfView(double length, double aperture)
    {
        return 360.0 / M_PI * atan(aperture/(2.0*length));
    }
    /// @brief Return the focal length in mm given a horizontal field of
    /// view in degrees and the specified aperture in mm.
    static double fieldOfViewToFocalLength(double fov, double aperture)
    {
        return aperture/(2.0*(tan(fov * M_PI / 360.0)));
    }
};// PerspectiveCamera


class OrthographicCamera: public BaseCamera
{
public:
    /// @brief Constructor
    /// @param film         film (i.e. image) defining the pixel resolution
    /// @param rotation     rotation in degrees of the camera in world space
    ///                     (applied in x, y, z order)
    /// @param translation  translation of the camera in world-space units,
    ///                     applied after rotation
    /// @param frameWidth   width in of the frame in world-space units
    /// @param nearPlane    depth of the near clipping plane in world-space units
    /// @param farPlane     depth of the far clipping plane in world-space units
    ///
    /// @details If no rotation or translation is provided, the camera is placed
    /// at (0,0,0) in world space and points in the direction of the negative z axis.
    OrthographicCamera(Film& film,
                       const Vec3R& rotation    = Vec3R(0.0),
                       const Vec3R& translation = Vec3R(0.0),
                       double frameWidth = 1.0,
                       double nearPlane  = 1e-3,
                       double farPlane   = std::numeric_limits<double>::max())
        : BaseCamera(film, rotation, translation, 0.5*frameWidth, nearPlane, farPlane)
    {
    }
    virtual ~OrthographicCamera() {}

    virtual math::Ray<double> getRay(
        size_t i, size_t j, double iOffset = 0.5, double jOffset = 0.5) const
    {
        math::Ray<double> ray(mRay);
        Vec3R eye = BaseCamera::rasterToScreen(i + iOffset, j + jOffset, 0.0);
        ray.setEye(BaseCamera::mScreenToWorld.applyMap(eye));
        return ray;
    }
};// OrthographicCamera


//////////////////////////////////////// SHADERS ////////////////////////////////////////


/// Abstract base class for the shaders
class BaseShader
{
public:
    typedef math::Ray<Real> RayT;
    BaseShader() {}
    virtual ~BaseShader() {}
    /// @brief Defines the interface of the virtual function that returns a RGB color.
    /// @param xyz World position of the intersection point.
    /// @param nml Normal in world space at the intersection point.
    /// @param dir Direction of the ray in world space.
    virtual Film::RGBA operator()(const Vec3R& xyz, const Vec3R& nml, const Vec3R& dir) const = 0;
    virtual BaseShader* copy() const = 0;
};


/// Shader that produces a simple matte
class MatteShader: public BaseShader
{
public:
    MatteShader(const Film::RGBA& c = Film::RGBA(1.0f)): mRGBA(c) {}
    virtual ~MatteShader() {}
    virtual Film::RGBA operator()(const Vec3R&, const Vec3R&, const Vec3R&) const
    {
        return mRGBA;
    }
    virtual BaseShader* copy() const { return new MatteShader(*this); }

private:
    const Film::RGBA mRGBA;
};


/// Color shader that treats the surface normal (x, y, z) as an RGB color
class NormalShader: public BaseShader
{
public:
    NormalShader(const Film::RGBA& c = Film::RGBA(1.0f)) : mRGBA(c*0.5f) {}
    virtual ~NormalShader() {}
    virtual Film::RGBA operator()(const Vec3R&, const Vec3R& normal, const Vec3R&) const
    {
        return mRGBA*Film::RGBA(normal[0]+1.0f, normal[1]+1.0f, normal[2]+1.0f);
    }
    virtual BaseShader* copy() const { return new NormalShader(*this); }

private:
    const Film::RGBA mRGBA;
};

  
/// Color shader that treats position (x, y, z) as an RGB color in a
/// cube defined from an axis-aligned bounding box in world space.
class PositionShader: public BaseShader
{
public:
    PositionShader(const math::BBox<Vec3R>& bbox, const Film::RGBA& c = Film::RGBA(1.0f))
        : mMin(bbox.min()), mInvDim(1.0/bbox.extents()), mRGBA(c) {}
    virtual ~PositionShader() {}
    virtual Film::RGBA operator()(const Vec3R& xyz, const Vec3R&, const Vec3R&) const
    {
        const Vec3R rgb = (xyz - mMin)*mInvDim;
        return mRGBA*Film::RGBA(rgb[0], rgb[1], rgb[2]);
    }
    virtual BaseShader* copy() const { return new PositionShader(*this); }

    private:
    const Vec3R mMin, mInvDim;
    const Film::RGBA mRGBA;
};

/// @brief Simple diffuse Lambertian surface shader
/// @details Diffuse simply means the color is constant (e.g., white), and
/// Lambertian implies that the (radiant) intensity is directly proportional
/// to the cosine of the angle between the surface normal and the direction
/// of the light source.
class DiffuseShader: public BaseShader
{
public:
    DiffuseShader(const Film::RGBA& d = Film::RGBA(1.0f)): mRGBA(d) {}
    virtual Film::RGBA operator()(const Vec3R&, const Vec3R& normal, const Vec3R& rayDir) const
    {
        // We assume a single directional light source at the camera,
        // so the cosine of the angle between the surface normal and the
        // direction of the light source becomes the dot product of the
        // surface normal and inverse direction of the ray.  We also ignore
        // negative dot products, corresponding to strict one-sided shading.
        //return mRGBA * math::Max(0.0, normal.dot(-rayDir));

        // We take the abs of the dot product corresponding to having
        // light sources at +/- rayDir, i.e., two-sided shading.
        return mRGBA * math::Abs(normal.dot(rayDir));
    }
    virtual BaseShader* copy() const { return new DiffuseShader(*this); }

private:
    const Film::RGBA mRGBA;
};


//////////////////////////////////////// RAYTRACER ////////////////////////////////////////

template<typename GridT>
inline void rayTrace(const GridT& grid,
                     const BaseShader& shader,
                     BaseCamera& camera,
                     size_t pixelSamples,
                     unsigned int seed,
                     bool threaded)
{
    LevelSetRayTracer<GridT, tools::LevelSetRayIntersector<GridT> >
        tracer(grid, shader, camera, pixelSamples, seed);
    tracer.render(threaded);
}


template<typename GridT, typename IntersectorT>
inline void rayTrace(const GridT&,
                     const IntersectorT& inter,
                     const BaseShader& shader,
                     BaseCamera& camera,
                     size_t pixelSamples,
                     unsigned int seed,
                     bool threaded)
{
    LevelSetRayTracer<GridT, IntersectorT> tracer(inter, shader, camera, pixelSamples, seed);
    tracer.render(threaded);
}


////////////////////////////////////////


template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
LevelSetRayTracer(const GridT& grid,
                  const BaseShader& shader,
                  BaseCamera& camera,
                  size_t pixelSamples,
                  unsigned int seed)
    : mIsMaster(true),
      mRand(NULL),
      mInter(grid),
      mShader(shader.copy()),
      mCamera(&camera)
{
    this->setPixelSamples(pixelSamples, seed);
}

template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
LevelSetRayTracer(const IntersectorT& inter,
                  const BaseShader& shader,
                  BaseCamera& camera,
                  size_t pixelSamples,
                  unsigned int seed)
    : mIsMaster(true),
      mRand(NULL),
      mInter(inter),
      mShader(shader.copy()),
      mCamera(&camera)
{
    this->setPixelSamples(pixelSamples, seed);
}

template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
LevelSetRayTracer(const LevelSetRayTracer& other) :
    mIsMaster(false),
    mRand(other.mRand),
    mInter(other.mInter),
    mShader(other.mShader->copy()),
    mCamera(other.mCamera),
    mSubPixels(other.mSubPixels)
{
}

template<typename GridT, typename IntersectorT>
inline LevelSetRayTracer<GridT, IntersectorT>::
~LevelSetRayTracer()
{
    if (mIsMaster) delete [] mRand;
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setGrid(const GridT& grid)
{
    assert(mIsMaster);
    mInter = IntersectorT(grid);
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setIntersector(const IntersectorT& inter)
{
    assert(mIsMaster);
    mInter = inter;
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setShader(const BaseShader& shader)
{
    assert(mIsMaster);
    mShader.reset(shader.copy());
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setCamera(BaseCamera& camera)
{
    assert(mIsMaster);
    mCamera = &camera;
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
setPixelSamples(size_t pixelSamples, unsigned int seed)
{
    assert(mIsMaster);
    if (pixelSamples == 0) {
        OPENVDB_THROW(ValueError, "pixelSamples must be larger then zero!");
    }
    mSubPixels = pixelSamples - 1;
    delete [] mRand;
    if (mSubPixels > 0) {
        mRand = new double[16];
        math::Rand01<double> rand(seed);//offsets for anti-aliaing by jittered super-sampling
        for (size_t i=0; i<16; ++i) mRand[i] = rand();
    } else {
        mRand = NULL;
    }
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
render(bool threaded) const
{
    tbb::blocked_range<size_t> range(0, mCamera->height());
    threaded ? tbb::parallel_for(range, *this) : (*this)(range);
}

template<typename GridT, typename IntersectorT>
inline void LevelSetRayTracer<GridT, IntersectorT>::
operator()(const tbb::blocked_range<size_t>& range) const
{
    const BaseShader& shader = *mShader;
    Vec3Type xyz, nml;
    const float frac = 1.0f / (1.0f + mSubPixels);
    for (size_t j=range.begin(), n=0, je = range.end(); j<je; ++j) {
        for (size_t i=0, ie = mCamera->width(); i<ie; ++i) {
            Film::RGBA& bg = mCamera->pixel(i,j);
            RayType ray = mCamera->getRay(i, j);//primary ray
            Film::RGBA c = mInter.intersectsWS(ray, xyz, nml) ? shader(xyz, nml, ray.dir()) : bg;
            for (size_t k=0; k<mSubPixels; ++k, n +=2 ) {
                ray = mCamera->getRay(i, j, mRand[n & 15], mRand[(n+1) & 15]);
                c += mInter.intersectsWS(ray, xyz, nml) ? shader(xyz, nml, ray.dir()) : bg;
            }//loop over sub-pixels
            bg = c*frac;
        }//loop over image height
    }//loop over image width
}

////////////////////////////////////////

template<typename IntersectorT, typename SampleT>
inline VolumeRender<IntersectorT, SampleT>::
VolumeRender(const IntersectorT& inter, BaseCamera& camera)
    : mCamera(&camera)
    , mIntersector(&inter)
    , mPrimaryStep(1.0)
    , mShadowStep(3.0)
    , mCutOff(0.005)
    , mLightGain(1.0)
    , mLightDir(Vec3R(0.3, 0.3, 0).unit())
    , mLightColor(0.7, 0.7, 0.7)
    , mAbsorption(0.1)
    , mScattering(1.5)
{
}

template<typename IntersectorT, typename SampleT>
inline void VolumeRender<IntersectorT, SampleT>::
render(bool threaded) const
{
    tbb::blocked_range<size_t> range(0, mCamera->height());
    threaded ? tbb::parallel_for(range, *this) : (*this)(range);
}

template<typename IntersectorT, typename SampleT>
inline void VolumeRender<IntersectorT, SampleT>::
operator()(const tbb::blocked_range<size_t>& range) const
{
    IntersectorT primary(*mIntersector), shadow(*mIntersector);
    SamplerType sampler(shadow.grid().getConstAccessor(), shadow.grid().transform());
        
    // Any variable prefixed with p (or s) means it's associate with a primay (or shadow) ray
    const Vec3R lightDir = mLightDir, lightAmp = mLightColor;
    const Vec3R pScat = mScattering, sigma = -(pScat+mAbsorption);//Absorption and scattering
    const Real sGain = mLightGain;//in-scattering along shadow ray 
    const Real pStep = mPrimaryStep;//Integration step along primary ray in voxel units
    const Real sStep = mShadowStep;//Integration step along shadow ray in voxel units
    const Real cutoff = mCutOff;//Cutoff for density and transmittance
    
    RayType sRay(Vec3R(0), lightDir);//Shadow ray
    for (size_t j=range.begin(), je = range.end(); j<je; ++j) {
        for (size_t i=0, ie = mCamera->width(); i<ie; ++i) {
            Film::RGBA& bg = mCamera->pixel(i, j);
            bg.a = bg.r = bg.g = bg.b = 0;
            RayType pRay = mCamera->getRay(i, j);// Primary ray
            if( !primary.setWorldRay(pRay)) continue;
            Vec3R pTrans(1.0), pLumi(0.0);
            for (Real pT0, pT1; primary.march(pT0, pT1); ) {
                for (Real pT = pStep*ceil(pT0/pStep); pT <= pT1; pT +=pStep) {
                    Vec3R pPos = primary.getWorldPos(pT);
                    const Real density = sampler.wsSample(pPos);
                    if (density < cutoff) continue;
                    pTrans *= math::Exp(sigma * density * pStep);
                    if (pTrans.lengthSqr()<cutoff) goto Pixel;//Terminate pRay
                    Vec3R sTrans(1.0);
                    sRay.setEye(pPos);
                    if( !shadow.setWorldRay(sRay)) continue;
                    for (Real sT0, sT1, gain=1.0; shadow.march(sT0, sT1);) {
                        for (Real sT = sStep*ceil(sT0/sStep); sT<=sT1; sT+=sStep, gain+=sGain) {
                            const Real d = sampler.wsSample(shadow.getWorldPos(sT));
                            if (d < cutoff) continue;
                            sTrans *= math::Exp(sigma * d * sStep/gain);
                            if (sTrans.lengthSqr()<cutoff) goto Luminance;//Terminate sRay 
                        }//Integration over shadow segment
                    }// Shadow ray march
                Luminance:
                    pLumi += (pScat * density * sTrans * pStep * pTrans) * lightAmp;
                }//Integration over primary segment
            }// Primary ray march
        Pixel:
            bg.r = pLumi[0];
            bg.g = pLumi[1];
            bg.b = pLumi[2];
            bg.a = 1.0f - pTrans.sum()/3.0f;
        }//Horizontal pixel scan    
    }//Vertical pixel scan
}

} // namespace tools
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_TOOLS_RAYTRACER_HAS_BEEN_INCLUDED

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
