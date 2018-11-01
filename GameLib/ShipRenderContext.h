/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2018-03-22
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "GameOpenGL.h"
#include "GameTypes.h"
#include "ImageData.h"
#include "RenderCore.h"
#include "ShaderManager.h"
#include "SysSpecifics.h"
#include "TextureRenderManager.h"
#include "Vectors.h"

#include <array>
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Render {

class ShipRenderContext
{
public:

    ShipRenderContext(
        size_t pointCount,
        std::optional<ImageData> texture,
        ShaderManager<ShaderManagerTraits> & shaderManager,
        GameOpenGLTexture & textureAtlasOpenGLHandle,
        TextureAtlasMetadata const & textureAtlasMetadata,
        TextureRenderManager const & textureRenderManager,
        float const(&orthoMatrix)[4][4],
        float visibleWorldHeight,
        float visibleWorldWidth,
        float canvasToVisibleWorldHeightRatio,
        float ambientLightIntensity,
        float waterLevelOfDetail,
        ShipRenderMode shipRenderMode,
        VectorFieldRenderMode vectorFieldRenderMode,
        bool showStressedSprings,
        bool wireframeMode);
    
    ~ShipRenderContext();

public:

    void UpdateOrthoMatrix(float const(&orthoMatrix)[4][4]);

    void UpdateVisibleWorldCoordinates(
        float visibleWorldHeight,
        float visibleWorldWidth,
        float canvasToVisibleWorldHeightRatio);

    void UpdateAmbientLightIntensity(float ambientLightIntensity);

    void UpdateWaterLevelThreshold(float waterLevelOfDetail);

    void UpdateShipRenderMode(ShipRenderMode shipRenderMode);

    void UpdateVectorFieldRenderMode(VectorFieldRenderMode vectorFieldRenderMode);

    void UpdateShowStressedSprings(bool showStressedSprings);

    void UpdateWireframeMode(bool wireframeMode);

public:

    void RenderStart(std::vector<std::size_t> const & connectedComponentsMaxSizes);

    //
    // Points
    //

    void UploadPointImmutableGraphicalAttributes(
        vec3f const * restrict color,
        vec2f const * restrict textureCoordinates);

    void UploadPoints(
        vec2f const * restrict position,
        float const * restrict light,
        float const * restrict water);

    //
    // Elements
    //

    void UploadElementsStart();

    inline void UploadElementPoint(
        int pointIndex,
        ConnectedComponentId connectedComponentId)
    {
        size_t const connectedComponentIndex = connectedComponentId - 1;

        assert(connectedComponentIndex < mConnectedComponents.size());
        assert(mConnectedComponents[connectedComponentIndex].pointElementCount + 1u <= mConnectedComponents[connectedComponentIndex].pointElementMaxCount);

        PointElement * const pointElement = &(mConnectedComponents[connectedComponentIndex].pointElementBuffer[mConnectedComponents[connectedComponentIndex].pointElementCount]);

        pointElement->pointIndex = pointIndex;

        ++(mConnectedComponents[connectedComponentIndex].pointElementCount);
    }

    inline void UploadElementSpring(
        int pointIndex1,
        int pointIndex2,
        ConnectedComponentId connectedComponentId)
    {
        size_t const connectedComponentIndex = connectedComponentId - 1;

        assert(connectedComponentIndex < mConnectedComponents.size());
        assert(mConnectedComponents[connectedComponentIndex].springElementCount + 1u <= mConnectedComponents[connectedComponentIndex].springElementMaxCount);

        SpringElement * const springElement = &(mConnectedComponents[connectedComponentIndex].springElementBuffer[mConnectedComponents[connectedComponentIndex].springElementCount]);

        springElement->pointIndex1 = pointIndex1;
        springElement->pointIndex2 = pointIndex2;

        ++(mConnectedComponents[connectedComponentIndex].springElementCount);
    }

    inline void UploadElementRope(
        int pointIndex1,
        int pointIndex2,
        ConnectedComponentId connectedComponentId)
    {
        size_t const connectedComponentIndex = connectedComponentId - 1;

        assert(connectedComponentIndex < mConnectedComponents.size());
        assert(mConnectedComponents[connectedComponentIndex].ropeElementCount + 1u <= mConnectedComponents[connectedComponentIndex].ropeElementMaxCount);

        RopeElement * const ropeElement = &(mConnectedComponents[connectedComponentIndex].ropeElementBuffer[mConnectedComponents[connectedComponentIndex].ropeElementCount]);

        ropeElement->pointIndex1 = pointIndex1;
        ropeElement->pointIndex2 = pointIndex2;

        ++(mConnectedComponents[connectedComponentIndex].ropeElementCount);
    }

    inline void UploadElementTriangle(
        int pointIndex1,
        int pointIndex2,
        int pointIndex3,
        ConnectedComponentId connectedComponentId)
    {
        size_t const connectedComponentIndex = connectedComponentId - 1;

        assert(connectedComponentIndex < mConnectedComponents.size());
        assert(mConnectedComponents[connectedComponentIndex].triangleElementCount + 1u <= mConnectedComponents[connectedComponentIndex].triangleElementMaxCount);

        TriangleElement * const triangleElement = &(mConnectedComponents[connectedComponentIndex].triangleElementBuffer[mConnectedComponents[connectedComponentIndex].triangleElementCount]);

        triangleElement->pointIndex1 = pointIndex1;
        triangleElement->pointIndex2 = pointIndex2;
        triangleElement->pointIndex3 = pointIndex3;

        ++(mConnectedComponents[connectedComponentIndex].triangleElementCount);
    }

    void UploadElementsEnd();
    
    void UploadElementStressedSpringsStart();

    inline void UploadElementStressedSpring(
        int pointIndex1,
        int pointIndex2,
        ConnectedComponentId connectedComponentId)
    {
        size_t const connectedComponentIndex = connectedComponentId - 1;

        assert(connectedComponentIndex < mConnectedComponents.size());
        assert(mConnectedComponents[connectedComponentIndex].stressedSpringElementCount + 1u <= mConnectedComponents[connectedComponentIndex].stressedSpringElementMaxCount);

        StressedSpringElement * const stressedSpringElement = &(mConnectedComponents[connectedComponentIndex].stressedSpringElementBuffer[mConnectedComponents[connectedComponentIndex].stressedSpringElementCount]);

        stressedSpringElement->pointIndex1 = pointIndex1;
        stressedSpringElement->pointIndex2 = pointIndex2;

        ++(mConnectedComponents[connectedComponentIndex].stressedSpringElementCount);
    }

    void UploadElementStressedSpringsEnd();

    //
    // Generic textures
    //

    inline void UploadGenericTextureRenderSpecification(
        ConnectedComponentId connectedComponentId,
        TextureFrameId const & textureFrameId,
        vec2f const & position)
    {
        UploadGenericTextureRenderSpecification(
            connectedComponentId,
            textureFrameId,
            position,
            1.0f,
            0.0f);
    }

    inline void UploadGenericTextureRenderSpecification(
        ConnectedComponentId connectedComponentId,
        TextureFrameId const & textureFrameId,
        vec2f const & position,
        float scale,
        vec2f const & rotationBase,
        vec2f const & rotationOffset)
    {
        UploadGenericTextureRenderSpecification(
            connectedComponentId,
            textureFrameId,
            position,
            scale,
            rotationBase.angle(rotationOffset));
    }

    inline void UploadGenericTextureRenderSpecification(
        ConnectedComponentId connectedComponentId,
        TextureFrameId const & textureFrameId,
        vec2f const & position,
        float scale,
        float angle)
    {
        size_t const connectedComponentIndex = connectedComponentId - 1;

        assert(connectedComponentIndex < mConnectedComponentGenericTextureInfos.size());

        // Store <Index in TexturePolygonVertices, textureFrameId> in per-connected component data
        mConnectedComponentGenericTextureInfos[connectedComponentIndex].emplace_back(
            mGenericTextureRenderPolygonVertexBuffer.size(),
            textureFrameId);

        //
        // Populate the texture quad
        //

        TextureFrameMetadata const & frameMetadata = mTextureRenderManager.GetFrameMetadata(textureFrameId);

        float const leftX = -frameMetadata.AnchorWorldX;
        float const rightX = frameMetadata.WorldWidth - frameMetadata.AnchorWorldX;
        float const topY = frameMetadata.WorldHeight - frameMetadata.AnchorWorldY;
        float const bottomY = -frameMetadata.AnchorWorldY;

        float const lightSensitivity =
            frameMetadata.HasOwnAmbientLight ? 0.0f : 1.0f;

        // Append vertices

        float const dx = .5f / static_cast<float>(frameMetadata.Size.Width);
        float const dy = .5f / static_cast<float>(frameMetadata.Size.Height);

        // Top-left
        mGenericTextureRenderPolygonVertexBuffer.emplace_back(
            position,
            vec2f(leftX, topY),
            vec2f(dx, 1.0f - dy),
            scale,
            angle,
            1.0f,
            lightSensitivity);

        // Top-Right
        mGenericTextureRenderPolygonVertexBuffer.emplace_back(
            position,
            vec2f(rightX, topY),
            vec2f(1.0f - dx, 1.0f - dy),
            scale,
            angle,
            1.0f,
            lightSensitivity);

        // Bottom-left
        mGenericTextureRenderPolygonVertexBuffer.emplace_back(
            position,
            vec2f(leftX, bottomY),
            vec2f(dx, dy),
            scale,
            angle,
            1.0f,
            lightSensitivity);

        // Bottom-right
        mGenericTextureRenderPolygonVertexBuffer.emplace_back(
            position,
            vec2f(rightX, bottomY),
            vec2f(1.0f - dx, dy),
            scale,
            angle,
            1.0f,
            lightSensitivity);
    }


    //
    // Vectors
    //

    void UploadVectors(
        size_t count,
        vec2f const * restrict position,
        vec2f const * restrict vector,
        float lengthAdjustment,
        vec4f const & color);    

    void RenderEnd();

private:
    
    struct ConnectedComponentData;
    struct GenericTextureInfo;

    void RenderPointElements(ConnectedComponentData const & connectedComponent);

    void RenderSpringElements(
        ConnectedComponentData const & connectedComponent,
        bool withTexture);

    void RenderRopeElements(ConnectedComponentData const & connectedComponent);

    void RenderTriangleElements(
        ConnectedComponentData const & connectedComponent,
        bool withTexture);

    void RenderStressedSpringElements(ConnectedComponentData const & connectedComponent);

    void RenderGenericTextures(std::vector<GenericTextureInfo> const & connectedComponent);

    void RenderVectors();

private:

    //
    // Parameters
    //

    float mCanvasToVisibleWorldHeightRatio;
    float mAmbientLightIntensity;
    float mWaterLevelThreshold;
    ShipRenderMode mShipRenderMode;
    VectorFieldRenderMode mVectorFieldRenderMode;
    bool mShowStressedSprings;
    bool mWireframeMode;

private:

    ShaderManager<ShaderManagerTraits> & mShaderManager;


    //
    // Ship textures
    //

    GameOpenGLTexture mElementShipTexture;
    GameOpenGLTexture mElementStressedSpringTexture;

    //
    // Points
    //
    
    size_t const mPointCount;

    GameOpenGLVBO mPointPositionVBO;
    GameOpenGLVBO mPointLightVBO;
    GameOpenGLVBO mPointWaterVBO;
    GameOpenGLVBO mPointColorVBO;
    GameOpenGLVBO mPointElementTextureCoordinatesVBO;
    
    //
    // Generic Textures
    //

    GameOpenGLTexture & mTextureAtlasOpenGLHandle;
    TextureAtlasMetadata const & mTextureAtlasMetadata;
    // TODO: nuke
    TextureRenderManager const & mTextureRenderManager;

#pragma pack(push)
struct TextureRenderPolygonVertex
{
    vec2f centerPosition;
    vec2f vertexOffset;
    vec2f textureCoordinate;
    
    float scale;
    float angle;
    float transparency;
    float ambientLightSensitivity;

    TextureRenderPolygonVertex(
        vec2f _centerPosition,
        vec2f _vertexOffset,
        vec2f _textureCoordinate,
        float _scale,
        float _angle,
        float _transparency,
        float _ambientLightSensitivity)
        : centerPosition(_centerPosition)
        , vertexOffset(_vertexOffset)
        , textureCoordinate(_textureCoordinate)
        , scale(_scale)
        , angle(_angle)
        , transparency(_transparency)
        , ambientLightSensitivity(_ambientLightSensitivity)
    {}
};
#pragma pack(pop)

    struct GenericTextureInfo
    {
        size_t polygonIndex;
        TextureFrameId frameId;

        GenericTextureInfo(
            size_t _polygonIndex,
            TextureFrameId _frameId)
            : polygonIndex(_polygonIndex)
            , frameId(_frameId)
        {}
    };

    std::vector<std::vector<GenericTextureInfo>> mConnectedComponentGenericTextureInfos;

    std::vector<TextureRenderPolygonVertex> mGenericTextureRenderPolygonVertexBuffer;
    GameOpenGLVBO mGenericTextureRenderPolygonVertexVBO;


    //
    // Connected component data
    //

    std::vector<std::size_t> mConnectedComponentsMaxSizes;

#pragma pack(push)
    struct PointElement
    {
        int pointIndex;
    };
#pragma pack(pop)

#pragma pack(push)
    struct SpringElement
    {
        int pointIndex1;
        int pointIndex2;
    };
#pragma pack(pop)

#pragma pack(push)
    struct RopeElement
    {
        int pointIndex1;
        int pointIndex2;
    };
#pragma pack(pop)

#pragma pack(push)
    struct TriangleElement
    {
        int pointIndex1;
        int pointIndex2;
        int pointIndex3;
    };
#pragma pack(pop)

#pragma pack(push)
    struct StressedSpringElement
    {
        int pointIndex1;
        int pointIndex2;
    };
#pragma pack(pop)

    
    //
    // All the data that belongs to a single connected component
    //

    struct ConnectedComponentData
    {
        size_t pointElementCount;
        size_t pointElementMaxCount;
        std::unique_ptr<PointElement[]> pointElementBuffer;
        GameOpenGLVBO pointElementVBO;

        size_t springElementCount;
        size_t springElementMaxCount;
        std::unique_ptr<SpringElement[]> springElementBuffer;
        GameOpenGLVBO springElementVBO;

        size_t ropeElementCount;
        size_t ropeElementMaxCount;
        std::unique_ptr<RopeElement[]> ropeElementBuffer;
        GameOpenGLVBO ropeElementVBO;

        size_t triangleElementCount;
        size_t triangleElementMaxCount;
        std::unique_ptr<TriangleElement[]> triangleElementBuffer;
        GameOpenGLVBO triangleElementVBO;

        size_t stressedSpringElementCount;
        size_t stressedSpringElementMaxCount;
        std::unique_ptr<StressedSpringElement[]> stressedSpringElementBuffer;
        GameOpenGLVBO stressedSpringElementVBO;

        ConnectedComponentData()
            : pointElementCount(0)
            , pointElementMaxCount(0)
            , pointElementBuffer()
            , pointElementVBO()
            , springElementCount(0)
            , springElementMaxCount(0)
            , springElementBuffer()
            , springElementVBO()
            , ropeElementCount(0)
            , ropeElementMaxCount(0)
            , ropeElementBuffer()
            , ropeElementVBO()
            , triangleElementCount(0)
            , triangleElementMaxCount(0)
            , triangleElementBuffer()
            , triangleElementVBO()
            , stressedSpringElementCount(0)
            , stressedSpringElementMaxCount(0)
            , stressedSpringElementBuffer()
            , stressedSpringElementVBO()
        {}
    };

    std::vector<ConnectedComponentData> mConnectedComponents;


    //
    // Vectors
    //

    std::vector<vec2f> mVectorArrowPointPositionBuffer;
    GameOpenGLVBO mVectorArrowPointPositionVBO;
    vec4f mVectorArrowColor;
};

}