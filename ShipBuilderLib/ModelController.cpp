/***************************************************************************************
* Original Author:      Gabriele Giuseppini
* Created:              2021-08-28
* Copyright:            Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#include "ModelController.h"

#include <cassert>
#include <queue>

namespace ShipBuilder {

std::unique_ptr<ModelController> ModelController::CreateNew(
    ShipSpaceSize const & shipSpaceSize,
    std::string const & shipName,
    ShipTexturizer const & shipTexturizer)
{
    Model model = Model(shipSpaceSize, shipName);

    return std::unique_ptr<ModelController>(
        new ModelController(
            std::move(model),
            shipTexturizer));
}

std::unique_ptr<ModelController> ModelController::CreateForShip(
    ShipDefinition && shipDefinition,
    ShipTexturizer const & shipTexturizer)
{
    Model model = Model(std::move(shipDefinition));

    return std::unique_ptr<ModelController>(
        new ModelController(
            std::move(model),
            shipTexturizer));
}

ModelController::ModelController(
    Model && model,
    ShipTexturizer const & shipTexturizer)
    : mModel(std::move(model))
    , mShipTexturizer(shipTexturizer)
    , mMassParticleCount(0)
    , mTotalMass(0.0f)
    , mCenterOfMassSum(vec2f::zero())
    , mInstancedElectricalElementSet()
    , mElectricalParticleCount(0)
    /////
    , mGameVisualizationMode(GameVisualizationModeType::None)
    , mGameVisualizationAutoTexturizationTexture()
    , mGameVisualizationTexture()
    , mGameVisualizationTextureMagnificationFactor(0)
    , mStructuralLayerVisualizationMode(StructuralLayerVisualizationModeType::None)
    , mStructuralLayerVisualizationTexture()
    , mElectricalLayerVisualizationMode(ElectricalLayerVisualizationModeType::None)
    , mElectricalLayerVisualizationTexture()
    , mRopesLayerVisualizationMode(RopesLayerVisualizationModeType::None)
    , mTextureLayerVisualizationMode(TextureLayerVisualizationModeType::None)
    /////
    , mIsStructuralLayerInEphemeralVisualization(false)
    , mIsElectricalLayerInEphemeralVisualization(false)
    , mIsRopesLayerInEphemeralVisualization(false)
    , mIsTextureLayerInEphemeralVisualization(false)
{
    // Model is not dirty now
    assert(!mModel.GetIsDirty());

    // Initialize layers' analyses
    InitializeStructuralLayerAnalysis();
    InitializeElectricalLayerAnalysis();
    InitializeRopesLayerAnalysis();
}

ShipDefinition ModelController::MakeShipDefinition() const
{
    return mModel.MakeShipDefinition();
}

std::unique_ptr<RgbaImageData> ModelController::MakePreview() const
{
    // At this moment we can't but require a structural layer - there would be no preview without it
    assert(mModel.HasLayer(LayerType::Structural));

    auto previewTexture = std::make_unique<RgbaImageData>(ImageSize(mModel.GetShipSize().width, mModel.GetShipSize().height));

    RenderStructureInto(
        GetWholeShipRect(),
        *previewTexture);

    return previewTexture;
}

std::optional<ShipSpaceRect> ModelController::CalculateBoundingBox() const
{
    std::optional<ShipSpaceRect> boundingBox;

    //
    // Structural layer
    //

    assert(mModel.HasLayer(LayerType::Structural));

    StructuralLayerData const & structuralLayer = mModel.GetStructuralLayer();

    for (int y = 0; y < structuralLayer.Buffer.Size.height; ++y)
    {
        for (int x = 0; x < structuralLayer.Buffer.Size.width; ++x)
        {
            ShipSpaceCoordinates coords(x, y);

            if (structuralLayer.Buffer[{x, y}].Material != nullptr)
            {
                if (!boundingBox.has_value())
                    boundingBox = ShipSpaceRect(coords);
                else
                    boundingBox->UnionWith(coords);
            }
        }
    }

    //
    // Ropes layer
    //

    if (mModel.HasLayer(LayerType::Ropes))
    {
        for (auto const & e : mModel.GetRopesLayer().Buffer)
        {
            if (!boundingBox.has_value())
                boundingBox = ShipSpaceRect(e.StartCoords);
            else
                boundingBox->UnionWith(e.StartCoords);

            boundingBox->UnionWith(e.EndCoords);
        }
    }

    return boundingBox;
}

ModelValidationSession ModelController::StartValidation(Finalizer && finalizer) const
{
    return ModelValidationSession(
        mModel,
        std::move(finalizer));
}

std::optional<SampledInformation> ModelController::SampleInformationAt(ShipSpaceCoordinates const & coordinates, LayerType layer) const
{
    switch (layer)
    {
        case LayerType::Electrical:
        {
            assert(mModel.HasLayer(LayerType::Electrical));
            assert(!mIsElectricalLayerInEphemeralVisualization);
            assert(coordinates.IsInSize(GetShipSize()));

            ElectricalElement const & element = mModel.GetElectricalLayer().Buffer[coordinates];
            if (element.Material != nullptr)
            {
                assert((element.InstanceIndex != NoneElectricalElementInstanceIndex && element.Material->IsInstanced)
                    || (element.InstanceIndex == NoneElectricalElementInstanceIndex && !element.Material->IsInstanced));

                return SampledInformation(
                    element.Material->Name,
                    element.Material->IsInstanced ? element.InstanceIndex : std::optional<ElectricalElementInstanceIndex>());
            }
            else
            {
                return std::nullopt;
            }
        }

        case LayerType::Ropes:
        {
            assert(mModel.HasLayer(LayerType::Ropes));
            assert(!mIsRopesLayerInEphemeralVisualization);
            assert(coordinates.IsInSize(mModel.GetShipSize()));

            auto const * material = mModel.GetRopesLayer().Buffer.SampleMaterialEndpointAt(coordinates);
            if (material != nullptr)
            {
                return SampledInformation(material->Name, std::nullopt);
            }
            else
            {
                return std::nullopt;
            }
        }

        case LayerType::Structural:
        {
            assert(mModel.HasLayer(LayerType::Structural));
            assert(!mIsStructuralLayerInEphemeralVisualization);
            assert(coordinates.IsInSize(GetShipSize()));

            auto const * material = mModel.GetStructuralLayer().Buffer[coordinates].Material;
            if (material != nullptr)
            {
                return SampledInformation(material->Name, std::nullopt);
            }
            else
            {
                return std::nullopt;
            }
        }

        case LayerType::Texture:
        {
            // Nothing to do
            return std::nullopt;
        }
    }

    assert(false);
    return std::nullopt;
}

void ModelController::Flip(DirectionType direction)
{
    // Structural layer
    if (mModel.HasLayer(LayerType::Structural))
    {
        assert(!mIsStructuralLayerInEphemeralVisualization);

        mModel.GetStructuralLayer().Buffer.Flip(direction);

        RegisterDirtyVisualization<VisualizationType::StructuralLayer>(GetWholeShipRect());

        InitializeStructuralLayerAnalysis();
    }

    // Electrical layer
    if (mModel.HasLayer(LayerType::Electrical))
    {
        assert(!mIsElectricalLayerInEphemeralVisualization);

        mModel.GetElectricalLayer().Buffer.Flip(direction);

        RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(GetWholeShipRect());

        InitializeElectricalLayerAnalysis();
    }

    // Ropes layer
    if (mModel.HasLayer(LayerType::Ropes))
    {
        assert(!mIsRopesLayerInEphemeralVisualization);

        mModel.GetRopesLayer().Buffer.Flip(direction);

        RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

        InitializeRopesLayerAnalysis();
    }

    // Texture layer
    if (mModel.HasLayer(LayerType::Texture))
    {
        assert(!mIsTextureLayerInEphemeralVisualization);

        mModel.GetTextureLayer().Buffer.Flip(direction);

        RegisterDirtyVisualization<VisualizationType::TextureLayer>(GetWholeTextureRect());
    }

    //...and Game we do regardless, as there's always a structural layer at least
    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
}

void ModelController::Rotate90(RotationDirectionType direction)
{
    // Rotate size
    auto const originalSize = GetShipSize();
    mModel.SetShipSize(ShipSpaceSize(originalSize.height, originalSize.width));

    // Structural layer
    if (mModel.HasLayer(LayerType::Structural))
    {
        assert(!mIsStructuralLayerInEphemeralVisualization);

        mModel.GetStructuralLayer().Buffer.Rotate90(direction);

        mStructuralLayerVisualizationTexture.reset();
        RegisterDirtyVisualization<VisualizationType::StructuralLayer>(GetWholeShipRect());

        InitializeStructuralLayerAnalysis();
    }

    // Electrical layer
    if (mModel.HasLayer(LayerType::Electrical))
    {
        assert(!mIsElectricalLayerInEphemeralVisualization);

        mModel.GetElectricalLayer().Buffer.Rotate90(direction);

        mElectricalLayerVisualizationTexture.reset();
        RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(GetWholeShipRect());

        InitializeElectricalLayerAnalysis();
    }

    // Ropes layer
    if (mModel.HasLayer(LayerType::Ropes))
    {
        assert(!mIsRopesLayerInEphemeralVisualization);

        mModel.GetRopesLayer().Buffer.Rotate90(direction);

        RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

        InitializeRopesLayerAnalysis();
    }

    // Texture layer
    if (mModel.HasLayer(LayerType::Texture))
    {
        assert(!mIsTextureLayerInEphemeralVisualization);

        mModel.GetTextureLayer().Buffer.Rotate90(direction);

        RegisterDirtyVisualization<VisualizationType::TextureLayer>(GetWholeTextureRect());
    }

    //...and Game we do regardless, as there's always a structural layer at least
    mGameVisualizationTexture.reset();
    mGameVisualizationAutoTexturizationTexture.reset();
    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
}

void ModelController::ResizeShip(
    ShipSpaceSize const & newSize,
    ShipSpaceCoordinates const & originOffset)
{
    ShipSpaceSize const originalShipSize = mModel.GetShipSize();

    ShipSpaceRect newWholeShipRect({ 0, 0 }, newSize);

    mModel.SetShipSize(newSize);

    // Structural layer
    {
        assert(mModel.HasLayer(LayerType::Structural));

        assert(!mIsStructuralLayerInEphemeralVisualization);

        mModel.SetStructuralLayer(
            mModel.GetStructuralLayer().MakeReframed(
                newSize,
                originOffset,
                StructuralElement(nullptr)));

        InitializeStructuralLayerAnalysis();

        // Initialize visualization
        mStructuralLayerVisualizationTexture.reset();
        RegisterDirtyVisualization<VisualizationType::StructuralLayer>(newWholeShipRect);
    }

    // Electrical layer
    if (mModel.HasLayer(LayerType::Electrical))
    {
        assert(!mIsElectricalLayerInEphemeralVisualization);

        mModel.SetElectricalLayer(
            mModel.GetElectricalLayer().MakeReframed(
                newSize,
                originOffset,
                ElectricalElement(nullptr, NoneElectricalElementInstanceIndex)));

        InitializeElectricalLayerAnalysis();

        // Initialize visualization
        mElectricalLayerVisualizationTexture.reset();
        RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(newWholeShipRect);
    }

    // Ropes layer
    if (mModel.HasLayer(LayerType::Ropes))
    {
        assert(!mIsRopesLayerInEphemeralVisualization);

        mModel.SetRopesLayer(
            mModel.GetRopesLayer().MakeReframed(
                newSize,
                originOffset));

        InitializeRopesLayerAnalysis();

        RegisterDirtyVisualization<VisualizationType::RopesLayer>(newWholeShipRect);
    }

    // Texture layer
    if (mModel.HasLayer(LayerType::Texture))
    {
        assert(!mIsTextureLayerInEphemeralVisualization);

        // Convert to texture space
        vec2f const shipToTexture = GetShipSpaceToTextureSpaceFactor(originalShipSize, GetTextureSize());

        mModel.SetTextureLayer(
            mModel.GetTextureLayer().MakeReframed(
                ImageSize::FromFloatRound(newSize.ToFloat().scale(shipToTexture)),
                ImageCoordinates::FromFloatRound(originOffset.ToFloat().scale(shipToTexture)),
                rgbaColor(0, 0, 0, 0)));

        RegisterDirtyVisualization<VisualizationType::TextureLayer>(GetWholeTextureRect());
    }

    // Initialize game visualizations
    {
        mGameVisualizationTexture.reset();
        mGameVisualizationAutoTexturizationTexture.reset();
        RegisterDirtyVisualization<VisualizationType::Game>(newWholeShipRect);
    }

    assert(mModel.GetShipSize() == newSize);
    assert(GetWholeShipRect() == newWholeShipRect);
}

ShipLayers ModelController::Copy(
    ShipSpaceRect const & region,
    std::optional<LayerType> layerSelection) const
{
    // Structural
    std::unique_ptr<StructuralLayerData> structuralLayerCopy;
    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Structural))
    {
        structuralLayerCopy = std::make_unique<StructuralLayerData>(mModel.GetStructuralLayer().CloneRegion(region));
    }

    // Electrical
    std::unique_ptr<ElectricalLayerData> electricalLayerCopy;
    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Electrical))
    {
        electricalLayerCopy = std::make_unique<ElectricalLayerData>(mModel.GetElectricalLayer().CloneRegion(region));
    }

    // Ropes
    std::unique_ptr<RopesLayerData> ropesLayerCopy;
    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Ropes))
    {
        ropesLayerCopy = std::make_unique<RopesLayerData>(mModel.GetRopesLayer().Buffer.CopyRegion(region));
    }

    // Texture
    std::unique_ptr<TextureLayerData> textureLayerCopy;
    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Texture))
    {
        textureLayerCopy = std::make_unique<TextureLayerData>(mModel.GetTextureLayer().CloneRegion(ShipSpaceToTextureSpace(region)));
    }

    return ShipLayers(
        region.size,
        std::move(structuralLayerCopy),
        std::move(electricalLayerCopy),
        std::move(ropesLayerCopy),
        std::move(textureLayerCopy));
}

GenericUndoPayload ModelController::EraseRegion(
    ShipSpaceRect const & region,
    std::optional<LayerType> const & layerSelection)
{
    //
    // Prepare undo
    //

    GenericUndoPayload undoPayload = MakeGenericUndoPayload(
        region,
        CheckLayerSelectionApplicability(layerSelection, LayerType::Structural),
        CheckLayerSelectionApplicability(layerSelection, LayerType::Electrical),
        CheckLayerSelectionApplicability(layerSelection, LayerType::Ropes),
        CheckLayerSelectionApplicability(layerSelection, LayerType::Texture));

    //
    // Erase
    //

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Structural))
    {
        StructuralRegionFill(region, nullptr);
    }

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Electrical))
    {
        ElectricalRegionFill(region, nullptr);
    }

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Ropes))
    {
        EraseRopesRegion(region);
    }

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Texture))
    {
        EraseTextureRegion(ShipSpaceToTextureSpace(region));
    }

    return undoPayload;
}

GenericUndoPayload ModelController::Paste(
    ShipLayers const & sourcePayload,
    ShipSpaceCoordinates const & pasteOrigin,
    bool isTransparent)
{
    //
    // Structural, Electrical, Ropes
    //

    ShipSpaceCoordinates actualPasteOriginShip = pasteOrigin;

    std::optional<StructuralLayerData> structuralLayerRegionBackup;
    std::optional<ElectricalLayerData> electricalLayerRegionBackup;
    std::optional<RopesLayerData> ropesLayerRegionBackup;

    std::optional<ShipSpaceRect> const affectedTargetRectShip =
        ShipSpaceRect(pasteOrigin, sourcePayload.Size)
        .MakeIntersectionWith(GetWholeShipRect());

    if (affectedTargetRectShip.has_value())
    {
        assert(!affectedTargetRectShip->IsEmpty());

        // Adjust paste origin
        actualPasteOriginShip = affectedTargetRectShip->origin;

        // Calculate affected source region
        ShipSpaceRect const affectedSourceRegion(
            ShipSpaceCoordinates(0, 0) + (actualPasteOriginShip - pasteOrigin),
            affectedTargetRectShip->size);

        if (sourcePayload.StructuralLayer && mModel.HasLayer(LayerType::Structural))
        {
            assert(!mIsStructuralLayerInEphemeralVisualization);

            // Prepare undo
            structuralLayerRegionBackup = mModel.GetStructuralLayer().MakeRegionBackup(*affectedTargetRectShip);

            // Paste
            DoStructuralRegionBufferPaste(
                sourcePayload.StructuralLayer->Buffer,
                affectedSourceRegion,
                actualPasteOriginShip,
                isTransparent);

            // Re-initialize layer analysis
            InitializeStructuralLayerAnalysis();
        }

        if (sourcePayload.ElectricalLayer && mModel.HasLayer(LayerType::Electrical))
        {
            assert(!mIsElectricalLayerInEphemeralVisualization);

            // Prepare undo
            electricalLayerRegionBackup = mModel.GetElectricalLayer().MakeRegionBackup(*affectedTargetRectShip);

            // Paste
            DoElectricalRegionBufferPaste(
                sourcePayload.ElectricalLayer->Buffer,
                affectedSourceRegion,
                actualPasteOriginShip,
                [isTransparent, this, &sourcePayload](ElectricalElement const & src, ElectricalElement const & dst) -> ElectricalElement const &
                {
                    // Take care of deletion of old
                    if (dst.Material != nullptr && (src.Material != nullptr || !isTransparent))
                    {
                        // Old is deleted
                        --mElectricalParticleCount;

                        // See whether old was instanced
                        if (dst.InstanceIndex != NoneElectricalElementInstanceIndex)
                        {
                            // De-register instance ID
                            mInstancedElectricalElementSet.Remove(dst.InstanceIndex);

                            // Remove from panel (if there)
                            mModel.GetElectricalLayer().Panel.TryRemove(dst.InstanceIndex);
                        }
                    }

                    // Take care of creation of new
                    if (src.Material != nullptr)
                    {
                        // New is created
                        ++mElectricalParticleCount;

                        // See whether new is instanced
                        if (src.InstanceIndex != NoneElectricalElementInstanceIndex)
                        {
                            // Register new instance ID
                            auto newInstanceIndex = mInstancedElectricalElementSet.IsRegistered(src.InstanceIndex)
                                ? mInstancedElectricalElementSet.Add(src.Material) // Rename
                                : mInstancedElectricalElementSet.Register(src.InstanceIndex, src.Material); // Reuse

                            // Copy panel element - if any
                            if (sourcePayload.ElectricalLayer->Panel.Contains(src.InstanceIndex))
                            {
                                mModel.GetElectricalLayer().Panel.Add(
                                    newInstanceIndex,
                                    sourcePayload.ElectricalLayer->Panel[src.InstanceIndex]);
                            }
                        }
                    }

                    return isTransparent
                        ? (src.Material ? src : dst)
                        : src;
                });

            assert(static_cast<size_t>(std::count_if(
                mModel.GetElectricalLayer().Buffer.Data.get(),
                &(mModel.GetElectricalLayer().Buffer.Data.get()[mModel.GetElectricalLayer().Buffer.Size.GetLinearSize()]),
                [](ElectricalElement const & elem) -> bool
                {
                    return elem.Material != nullptr;
                })) == mElectricalParticleCount);
        }

        if (sourcePayload.RopesLayer && mModel.HasLayer(LayerType::Ropes))
        {
            assert(!mIsRopesLayerInEphemeralVisualization);

            // Prepare undo
            ropesLayerRegionBackup = mModel.GetRopesLayer().MakeRegionBackup(*affectedTargetRectShip);

            // Paste
            DoRopesRegionBufferPaste(
                sourcePayload.RopesLayer->Buffer,
                affectedSourceRegion,
                actualPasteOriginShip,
                isTransparent);
        }
    }

    //
    // Texture
    //

    std::optional<TextureLayerData> textureLayerRegionBackup;

    if (sourcePayload.TextureLayer && mModel.HasLayer(LayerType::Texture))
    {
        ImageCoordinates const pasteOriginTexture = ShipSpaceToTextureSpace(pasteOrigin);

        ImageCoordinates actualPasteOriginTexture = pasteOriginTexture;

        std::optional<ImageRect> const affectedTargetRectTexture =
            ImageRect(pasteOriginTexture, sourcePayload.TextureLayer->Buffer.Size)
            .MakeIntersectionWith(GetWholeTextureRect());

        if (affectedTargetRectTexture)
        {
            assert(!affectedTargetRectTexture->IsEmpty());

            // Adjust paste origin
            actualPasteOriginTexture = affectedTargetRectTexture->origin;

            // Calculate affected source region
            ImageRect const affectedSourceRegion(
                ImageCoordinates(0, 0) + (actualPasteOriginTexture - pasteOriginTexture),
                affectedTargetRectTexture->size);

            if (sourcePayload.TextureLayer && mModel.HasLayer(LayerType::Texture)) // Rotfl - just for code structure symmetry
            {
                assert(!mIsTextureLayerInEphemeralVisualization);

                // Prepare undo
                textureLayerRegionBackup = mModel.GetTextureLayer().MakeRegionBackup(*affectedTargetRectTexture);

                // Paste
                DoTextureRegionBufferPaste(
                    sourcePayload.TextureLayer->Buffer,
                    affectedSourceRegion,
                    *affectedTargetRectTexture,
                    actualPasteOriginTexture,
                    isTransparent);
            }
        }
    }

    return GenericUndoPayload(
        actualPasteOriginShip,
        std::move(structuralLayerRegionBackup),
        std::move(electricalLayerRegionBackup),
        std::move(ropesLayerRegionBackup),
        std::move(textureLayerRegionBackup));
}

void ModelController::Restore(GenericUndoPayload && undoPayload)
{
    // Note: no layer presence nor size changes

    for (LayerType const affectedLayerType : undoPayload.GetAffectedLayers())
    {
        switch (affectedLayerType)
        {
            case LayerType::Structural:
            {
                RestoreStructuralLayerRegionBackup(
                    std::move(*undoPayload.StructuralLayerRegionBackup),
                    undoPayload.Origin);

                break;
            }

            case LayerType::Electrical:
            {
                RestoreElectricalLayerRegionBackup(
                    std::move(*undoPayload.ElectricalLayerRegionBackup),
                    undoPayload.Origin);

                break;
            }

            case LayerType::Ropes:
            {
                RestoreRopesLayerRegionBackup(
                    std::move(*undoPayload.RopesLayerRegionBackup),
                    undoPayload.Origin);

                break;
            }

            case LayerType::Texture:
            {
                RestoreTextureLayerRegionBackup(
                    std::move(*undoPayload.TextureLayerRegionBackup),
                    ShipSpaceToTextureSpace(undoPayload.Origin));

                break;
            }
        }
    }
}

GenericEphemeralVisualizationRestorePayload ModelController::PasteForEphemeralVisualization(
    ShipLayers const & sourcePayload,
    ShipSpaceCoordinates const & pasteOrigin,
    bool isTransparent)
{
    //
    // Structural, Electrical, Ropes
    //

    ShipSpaceCoordinates actualPasteOriginShip = pasteOrigin;

    std::optional<typename LayerTypeTraits<LayerType::Structural>::buffer_type> structuralLayerUndoBufferRegion;
    std::optional<typename LayerTypeTraits<LayerType::Electrical>::buffer_type> electricalLayerUndoBufferRegion;
    std::optional<typename LayerTypeTraits<LayerType::Ropes>::buffer_type> ropesLayerUndoBuffer;

    std::optional<ShipSpaceRect> const affectedTargetRectShip =
        ShipSpaceRect(pasteOrigin, sourcePayload.Size)
        .MakeIntersectionWith(GetWholeShipRect());

    if (affectedTargetRectShip.has_value())
    {
        assert(!affectedTargetRectShip->IsEmpty());

        // Adjust paste origin
        actualPasteOriginShip = affectedTargetRectShip->origin;

        // Calculate affected source region
        ShipSpaceRect const affectedSourceRegion(
            ShipSpaceCoordinates(0, 0) + (actualPasteOriginShip - pasteOrigin),
            affectedTargetRectShip->size);

        if (sourcePayload.StructuralLayer && mModel.HasLayer(LayerType::Structural))
        {
            assert(!mIsStructuralLayerInEphemeralVisualization);

            // Prepare undo
            structuralLayerUndoBufferRegion = mModel.GetStructuralLayer().Buffer.CloneRegion(*affectedTargetRectShip);

            // Paste
            DoStructuralRegionBufferPaste(
                sourcePayload.StructuralLayer->Buffer,
                affectedSourceRegion,
                actualPasteOriginShip,
                isTransparent);

            // Remember we are in temp visualization now
            mIsStructuralLayerInEphemeralVisualization = true;
        }

        if (sourcePayload.ElectricalLayer && mModel.HasLayer(LayerType::Electrical))
        {
            assert(!mIsElectricalLayerInEphemeralVisualization);

            // Prepare undo
            electricalLayerUndoBufferRegion = mModel.GetElectricalLayer().Buffer.CloneRegion(*affectedTargetRectShip);

            // Paste
            DoElectricalRegionBufferPaste(
                sourcePayload.ElectricalLayer->Buffer,
                affectedSourceRegion,
                actualPasteOriginShip,
                isTransparent
                    ? [](ElectricalElement const & src, ElectricalElement const & dst) -> ElectricalElement const &
                    {
                        return src.Material
                            ? src
                            : dst;
                    }
                    : [](ElectricalElement const & src, ElectricalElement const &) -> ElectricalElement const &
                    {
                        return src;
                    });

            // Remember we are in temp visualization now
            mIsElectricalLayerInEphemeralVisualization = true;
        }

        if (sourcePayload.RopesLayer && mModel.HasLayer(LayerType::Ropes))
        {
            assert(!mIsRopesLayerInEphemeralVisualization);

            // Prepare undo
            ropesLayerUndoBuffer = mModel.GetRopesLayer().Buffer.Clone();

            // Paste
            DoRopesRegionBufferPaste(
                sourcePayload.RopesLayer->Buffer,
                affectedSourceRegion,
                actualPasteOriginShip,
                isTransparent);

            // Remember we are in temp visualization now
            mIsRopesLayerInEphemeralVisualization = true;
        }
    }

    //
    // Texture
    //

    std::optional<typename LayerTypeTraits<LayerType::Texture>::buffer_type> textureLayerUndoBufferRegion;

    if (sourcePayload.TextureLayer && mModel.HasLayer(LayerType::Texture))
    {
        ImageCoordinates const pasteOriginTexture = ShipSpaceToTextureSpace(pasteOrigin);

        ImageCoordinates actualPasteOriginTexture = pasteOriginTexture;

        std::optional<ImageRect> const affectedTargetRectTexture =
            ImageRect(pasteOriginTexture, sourcePayload.TextureLayer->Buffer.Size)
            .MakeIntersectionWith(GetWholeTextureRect());

        if (affectedTargetRectTexture)
        {
            assert(!affectedTargetRectTexture->IsEmpty());

            // Adjust paste origin
            actualPasteOriginTexture = affectedTargetRectTexture->origin;

            // Calculate affected source region
            ImageRect const affectedSourceRegion(
                ImageCoordinates(0, 0) + (actualPasteOriginTexture - pasteOriginTexture),
                affectedTargetRectTexture->size);

            if (sourcePayload.TextureLayer && mModel.HasLayer(LayerType::Texture)) // Rotfl - just for code structure symmetry
            {
                assert(!mIsTextureLayerInEphemeralVisualization);

                // Prepare undo
                textureLayerUndoBufferRegion = mModel.GetTextureLayer().Buffer.CloneRegion(*affectedTargetRectTexture);

                // Paste
                DoTextureRegionBufferPaste(
                    sourcePayload.TextureLayer->Buffer,
                    affectedSourceRegion,
                    *affectedTargetRectTexture,
                    actualPasteOriginTexture,
                    isTransparent);

                // Remember we are in temp visualization now
                mIsTextureLayerInEphemeralVisualization = true;
            }
        }
    }

    return GenericEphemeralVisualizationRestorePayload(
        actualPasteOriginShip,
        std::move(structuralLayerUndoBufferRegion),
        std::move(electricalLayerUndoBufferRegion),
        std::move(ropesLayerUndoBuffer),
        std::move(textureLayerUndoBufferRegion));
}

void ModelController::RestoreEphemeralVisualization(GenericEphemeralVisualizationRestorePayload && restorePayload)
{
    for (LayerType const affectedLayerType : restorePayload.GetAffectedLayers())
    {
        switch (affectedLayerType)
        {
            case LayerType::Structural:
            {
                RestoreStructuralLayerRegionEphemeralVisualization(
                    *restorePayload.StructuralLayerBufferRegion,
                    ShipSpaceRect(restorePayload.StructuralLayerBufferRegion->Size), // Whole backup
                    restorePayload.Origin);

                break;
            }

            case LayerType::Electrical:
            {
                RestoreElectricalLayerRegionEphemeralVisualization(
                    *restorePayload.ElectricalLayerBufferRegion,
                    ShipSpaceRect(restorePayload.ElectricalLayerBufferRegion->Size), // Whole backup
                    restorePayload.Origin);

                break;
            }

            case LayerType::Ropes:
            {
                RestoreRopesLayerEphemeralVisualization(*restorePayload.RopesLayerBuffer);

                break;
            }

            case LayerType::Texture:
            {
                RestoreTextureLayerRegionEphemeralVisualization(
                    *restorePayload.TextureLayerBufferRegion,
                    ImageRect(restorePayload.TextureLayerBufferRegion->Size), // Whole backup
                    ShipSpaceToTextureSpace(restorePayload.Origin));

                break;
            }
        }
    }
}

std::vector<LayerType> ModelController::CalculateAffectedLayers(ShipLayers const & otherSource) const
{
    std::vector<LayerType> affectedLayers;

    if (otherSource.StructuralLayer && mModel.HasLayer(LayerType::Structural))
    {
        affectedLayers.emplace_back(LayerType::Structural);
    }

    if (otherSource.ElectricalLayer && mModel.HasLayer(LayerType::Electrical))
    {
        affectedLayers.emplace_back(LayerType::Electrical);
    }

    if (otherSource.RopesLayer && mModel.HasLayer(LayerType::Ropes))
    {
        affectedLayers.emplace_back(LayerType::Ropes);
    }

    if (otherSource.TextureLayer && mModel.HasLayer(LayerType::Texture))
    {
        affectedLayers.emplace_back(LayerType::Texture);
    }

    return affectedLayers;
}

std::vector<LayerType> ModelController::CalculateAffectedLayers(std::optional<LayerType> const & layerSelection) const
{
    std::vector<LayerType> affectedLayers;

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Structural))
    {
        affectedLayers.emplace_back(LayerType::Structural);
    }

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Electrical))
    {
        affectedLayers.emplace_back(LayerType::Electrical);
    }

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Ropes))
    {
        affectedLayers.emplace_back(LayerType::Ropes);
    }

    if (CheckLayerSelectionApplicability(layerSelection, LayerType::Texture))
    {
        affectedLayers.emplace_back(LayerType::Texture);
    }

    return affectedLayers;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Structural
////////////////////////////////////////////////////////////////////////////////////////////////////

StructuralLayerData const & ModelController::GetStructuralLayer() const
{
    // This is used, among others, by WaterlineAnalyzer - at this moment
    // we require the structural alyer to be always present
    assert(mModel.HasLayer(LayerType::Structural));
    assert(!mIsStructuralLayerInEphemeralVisualization);

    return mModel.GetStructuralLayer();
}

void ModelController::SetStructuralLayer(StructuralLayerData && structuralLayer)
{
    mModel.SetStructuralLayer(std::move(structuralLayer));

    InitializeStructuralLayerAnalysis();

    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(GetWholeShipRect());

    mIsStructuralLayerInEphemeralVisualization = false;
}

std::unique_ptr<StructuralLayerData> ModelController::CloneStructuralLayer() const
{
    return mModel.CloneStructuralLayer();
}

StructuralMaterial const * ModelController::SampleStructuralMaterialAt(ShipSpaceCoordinates const & coords) const
{
    assert(mModel.HasLayer(LayerType::Structural));
    assert(!mIsStructuralLayerInEphemeralVisualization);

    assert(coords.IsInSize(mModel.GetShipSize()));

    return mModel.GetStructuralLayer().Buffer[coords].Material;
}

void ModelController::StructuralRegionFill(
    ShipSpaceRect const & region,
    StructuralMaterial const * material)
{
    assert(mModel.HasLayer(LayerType::Structural));
    assert(!mIsStructuralLayerInEphemeralVisualization);

    //
    // Update model
    //

    for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
    {
        for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
        {
            WriteParticle(ShipSpaceCoordinates(x, y), material);
        }
    }

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(region);
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(region);
}

GenericUndoPayload ModelController::StructuralRectangle(
    ShipSpaceRect const & rect,
    std::uint32_t lineSize,
    StructuralMaterial const * lineMaterial,
    StructuralMaterial const * fillMaterial)
{
    assert(mModel.HasLayer(LayerType::Structural));
    assert(!mIsStructuralLayerInEphemeralVisualization);

    //
    // Prepare undo
    //

    StructuralLayerData structuralLayerRegionBackup = mModel.GetStructuralLayer().MakeRegionBackup(rect);

    //
    // Update model
    //

    // TODOHERE
    (void)lineSize;
    (void)lineMaterial;
    (void)fillMaterial;

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(rect);
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(rect);

    return GenericUndoPayload(
        rect.origin,
        std::move(structuralLayerRegionBackup),
        std::nullopt,
        std::nullopt,
        std::nullopt);
}

std::optional<ShipSpaceRect> ModelController::StructuralFlood(
    ShipSpaceCoordinates const & start,
    StructuralMaterial const * material,
    bool doContiguousOnly)
{
    assert(mModel.HasLayer(LayerType::Structural));

    assert(!mIsStructuralLayerInEphemeralVisualization);

    //
    // Update model
    //

    std::optional<ShipSpaceRect> affectedRect = DoFlood<LayerType::Structural>(
        start,
        material,
        doContiguousOnly,
        mModel.GetStructuralLayer());

    if (affectedRect.has_value())
    {
        //
        // Update visualization
        //

        RegisterDirtyVisualization<VisualizationType::Game>(*affectedRect);
        RegisterDirtyVisualization<VisualizationType::StructuralLayer>(*affectedRect);
    }

    return affectedRect;
}

void ModelController::RestoreStructuralLayerRegionBackup(
    StructuralLayerData && sourceLayerRegionBackup,
    ShipSpaceCoordinates const & targetOrigin)
{
    assert(mModel.HasLayer(LayerType::Structural));

    assert(!mIsStructuralLayerInEphemeralVisualization);

    //
    // Restore model
    //

    ShipSpaceSize const regionSize = sourceLayerRegionBackup.Buffer.Size;

    mModel.GetStructuralLayer().RestoreRegionBackup(
        std::move(sourceLayerRegionBackup),
        targetOrigin);

    //
    // Re-initialize layer analysis
    //

    InitializeStructuralLayerAnalysis();

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(ShipSpaceRect(targetOrigin, regionSize));
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(ShipSpaceRect(targetOrigin, regionSize));
}

void ModelController::RestoreStructuralLayer(std::unique_ptr<StructuralLayerData> sourceLayer)
{
    assert(!mIsStructuralLayerInEphemeralVisualization);

    //
    // Restore model
    //

    mModel.RestoreStructuralLayer(std::move(sourceLayer));

    //
    // Re-initialize layer analysis
    //

    InitializeStructuralLayerAnalysis();

    //
    // Update visualization
    //

    mGameVisualizationTexture.reset();
    mGameVisualizationAutoTexturizationTexture.reset();
    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    mStructuralLayerVisualizationTexture.reset();
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(GetWholeShipRect());
}

void ModelController::StructuralRegionFillForEphemeralVisualization(
    ShipSpaceRect const & region,
    StructuralMaterial const * material)
{
    assert(mModel.HasLayer(LayerType::Structural));

    //
    // Update model with just material - no analyses
    //

    auto & structuralLayerBuffer = mModel.GetStructuralLayer().Buffer;

    for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
    {
        for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
        {
            structuralLayerBuffer[ShipSpaceCoordinates(x, y)].Material = material;
        }
    }

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(region);
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(region);

    // Remember we are in temp visualization now
    mIsStructuralLayerInEphemeralVisualization = true;
}

GenericEphemeralVisualizationRestorePayload ModelController::StructuralRectangleForEphemeralVisualization(
    ShipSpaceRect const & rect,
    std::uint32_t lineSize,
    StructuralMaterial const * lineMaterial,
    StructuralMaterial const * fillMaterial)
{
    assert(mModel.HasLayer(LayerType::Structural));

    //
    // Prepare undo
    //

    auto structuralLayerUndoBufferRegion = mModel.GetStructuralLayer().Buffer.CloneRegion(rect);

    //
    // Update model with just material - no analyses
    //

    // TODOHERE
    (void)lineSize;
    (void)lineMaterial;
    (void)fillMaterial;

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(rect);
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(rect);

    // Remember we are in temp visualization now
    mIsStructuralLayerInEphemeralVisualization = true;

    return GenericEphemeralVisualizationRestorePayload(
        rect.origin,
        std::move(structuralLayerUndoBufferRegion),
        std::nullopt,
        std::nullopt,
        std::nullopt);
}

void ModelController::RestoreStructuralLayerRegionEphemeralVisualization(
    typename LayerTypeTraits<LayerType::Structural>::buffer_type const & backupBuffer,
    ShipSpaceRect const & backupBufferRegion,
    ShipSpaceCoordinates const & targetOrigin)
{
    assert(mIsStructuralLayerInEphemeralVisualization);

    DoStructuralRegionBufferPaste(
        backupBuffer,
        backupBufferRegion,
        targetOrigin,
        false);

    // Remember we are not anymore in temp visualization
    mIsStructuralLayerInEphemeralVisualization = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Electrical
////////////////////////////////////////////////////////////////////////////////////////////////////

ElectricalPanel const & ModelController::GetElectricalPanel() const
{
    assert(mModel.HasLayer(LayerType::Electrical));

    return mModel.GetElectricalLayer().Panel;
}

void ModelController::SetElectricalLayer(ElectricalLayerData && electricalLayer)
{
    mModel.SetElectricalLayer(std::move(electricalLayer));

    InitializeElectricalLayerAnalysis();

    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(GetWholeShipRect());

    mIsElectricalLayerInEphemeralVisualization = false;
}

void ModelController::RemoveElectricalLayer()
{
    assert(mModel.HasLayer(LayerType::Electrical));

    mModel.RemoveElectricalLayer();

    InitializeElectricalLayerAnalysis();

    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(GetWholeShipRect());

    mIsElectricalLayerInEphemeralVisualization = false;
}

std::unique_ptr<ElectricalLayerData> ModelController::CloneElectricalLayer() const
{
    return mModel.CloneElectricalLayer();
}

ElectricalMaterial const * ModelController::SampleElectricalMaterialAt(ShipSpaceCoordinates const & coords) const
{
    assert(mModel.HasLayer(LayerType::Electrical));
    assert(!mIsElectricalLayerInEphemeralVisualization);

    assert(coords.IsInSize(mModel.GetShipSize()));

    return mModel.GetElectricalLayer().Buffer[coords].Material;
}

bool ModelController::IsElectricalParticleAllowedAt(ShipSpaceCoordinates const & coords) const
{
    assert(!mIsStructuralLayerInEphemeralVisualization);

    if (mModel.HasLayer(LayerType::Structural)
        && mModel.GetStructuralLayer().Buffer[coords].Material != nullptr)
    {
        return true;
    }

    if (mModel.HasLayer(LayerType::Ropes)
        && mModel.GetRopesLayer().Buffer.HasEndpointAt(coords))
    {
        return true;
    }

    return false;
}

std::optional<ShipSpaceRect> ModelController::TrimElectricalParticlesWithoutSubstratum()
{
    assert(mModel.HasLayer(LayerType::Electrical));

    assert(!mIsElectricalLayerInEphemeralVisualization);

    //
    // Update model
    //

    std::optional<ShipSpaceRect> affectedRect;

    StructuralLayerData const & structuralLayer = mModel.GetStructuralLayer();
    ElectricalLayerData const & electricalLayer = mModel.GetElectricalLayer();

    assert(structuralLayer.Buffer.Size == electricalLayer.Buffer.Size);

    ElectricalMaterial const * nullMaterial = nullptr;

    for (int y = 0; y < structuralLayer.Buffer.Size.height; ++y)
    {
        for (int x = 0; x < structuralLayer.Buffer.Size.width; ++x)
        {
            auto const coords = ShipSpaceCoordinates(x, y);
            if (electricalLayer.Buffer[coords].Material != nullptr
                && structuralLayer.Buffer[coords].Material == nullptr)
            {
                WriteParticle(coords, nullMaterial);

                if (!affectedRect.has_value())
                {
                    affectedRect = ShipSpaceRect(coords);
                }
                else
                {
                    affectedRect->UnionWith(coords);
                }
            }
        }
    }

    //
    // Update visualization
    //

    if (affectedRect.has_value())
    {
        RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(*affectedRect);
    }

    return affectedRect;
}

void ModelController::ElectricalRegionFill(
    ShipSpaceRect const & region,
    ElectricalMaterial const * material)
{
    assert(mModel.HasLayer(LayerType::Electrical));

    assert(!mIsElectricalLayerInEphemeralVisualization);

    //
    // Update model
    //

    for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
    {
        for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
        {
            WriteParticle(ShipSpaceCoordinates(x, y), material);
        }
    }

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(region);
}

void ModelController::RestoreElectricalLayerRegionBackup(
    ElectricalLayerData && sourceLayerRegionBackup,
    ShipSpaceCoordinates const & targetOrigin)
{
    assert(mModel.HasLayer(LayerType::Electrical));

    assert(!mIsElectricalLayerInEphemeralVisualization);

    //
    // Restore model
    //

    ShipSpaceSize const regionSize = sourceLayerRegionBackup.Buffer.Size;

    mModel.GetElectricalLayer().RestoreRegionBackup(
        std::move(sourceLayerRegionBackup),
        targetOrigin);

    //
    // Re-initialize layer analysis (and instance IDs)
    //

    InitializeElectricalLayerAnalysis();

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(ShipSpaceRect(targetOrigin, regionSize));
}

void ModelController::RestoreElectricalLayer(std::unique_ptr<ElectricalLayerData> sourceLayer)
{
    assert(!mIsElectricalLayerInEphemeralVisualization);

    //
    // Restore model
    //

    mModel.RestoreElectricalLayer(std::move(sourceLayer));

    //
    // Re-initialize layer analysis (and instance IDs)
    //

    InitializeElectricalLayerAnalysis();

    //
    // Update visualization
    //

    mElectricalLayerVisualizationTexture.reset();
    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(GetWholeShipRect());
}

void ModelController::ElectricalRegionFillForEphemeralVisualization(
    ShipSpaceRect const & region,
    ElectricalMaterial const * material)
{
    assert(mModel.HasLayer(LayerType::Electrical));

    //
    // Update model just with material - no instance ID, no analyses, no panel
    //

    auto & electricalLayerBuffer = mModel.GetElectricalLayer().Buffer;

    for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
    {
        for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
        {
            electricalLayerBuffer[ShipSpaceCoordinates(x, y)].Material = material;
        }
    }

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(region);

    // Remember we are in temp visualization now
    mIsElectricalLayerInEphemeralVisualization = true;
}

void ModelController::RestoreElectricalLayerRegionEphemeralVisualization(
    typename LayerTypeTraits<LayerType::Electrical>::buffer_type const & backupBuffer,
    ShipSpaceRect const & backupBufferRegion,
    ShipSpaceCoordinates const & targetOrigin)
{
    assert(mIsElectricalLayerInEphemeralVisualization);

    DoElectricalRegionBufferPaste(
        backupBuffer,
        backupBufferRegion,
        targetOrigin,
        [](ElectricalElement const & src, ElectricalElement const &) -> ElectricalElement const &
        {
            return src;
        });

    // Remember we are not anymore in temp visualization
    mIsElectricalLayerInEphemeralVisualization = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Ropes
////////////////////////////////////////////////////////////////////////////////////////////////////

void ModelController::SetRopesLayer(RopesLayerData && ropesLayer)
{
    mModel.SetRopesLayer(std::move(ropesLayer));

    InitializeRopesLayerAnalysis();

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

    mIsRopesLayerInEphemeralVisualization = false;
}

void ModelController::RemoveRopesLayer()
{
    assert(mModel.HasLayer(LayerType::Ropes));

    mModel.RemoveRopesLayer();

    InitializeRopesLayerAnalysis();

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

    mIsRopesLayerInEphemeralVisualization = false;
}

std::unique_ptr<RopesLayerData> ModelController::CloneRopesLayer() const
{
    return mModel.CloneRopesLayer();
}

StructuralMaterial const * ModelController::SampleRopesMaterialAt(ShipSpaceCoordinates const & coords) const
{
    assert(mModel.HasLayer(LayerType::Ropes));
    assert(!mIsRopesLayerInEphemeralVisualization);

    assert(coords.IsInSize(mModel.GetShipSize()));

    return mModel.GetRopesLayer().Buffer.SampleMaterialEndpointAt(coords);
}

std::optional<size_t> ModelController::GetRopeElementIndexAt(ShipSpaceCoordinates const & coords) const
{
    assert(mModel.HasLayer(LayerType::Ropes));

    assert(!mIsRopesLayerInEphemeralVisualization);

    auto const searchIt = std::find_if(
        mModel.GetRopesLayer().Buffer.cbegin(),
        mModel.GetRopesLayer().Buffer.cend(),
        [&coords](RopeElement const & e)
        {
            return coords == e.StartCoords
                || coords == e.EndCoords;
        });

    if (searchIt != mModel.GetRopesLayer().Buffer.cend())
    {
        return std::distance(mModel.GetRopesLayer().Buffer.cbegin(), searchIt);
    }
    else
    {
        return std::nullopt;
    }
}

void ModelController::AddRope(
    ShipSpaceCoordinates const & startCoords,
    ShipSpaceCoordinates const & endCoords,
    StructuralMaterial const * material)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    assert(!mIsRopesLayerInEphemeralVisualization);

    //
    // Update model
    //

    AppendRope(
        startCoords,
        endCoords,
        material);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
}

void ModelController::MoveRopeEndpoint(
    size_t ropeElementIndex,
    ShipSpaceCoordinates const & oldCoords,
    ShipSpaceCoordinates const & newCoords)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    assert(!mIsRopesLayerInEphemeralVisualization);

    //
    // Update model
    //

    assert(ropeElementIndex < mModel.GetRopesLayer().Buffer.GetElementCount());

    MoveRopeEndpoint(
        mModel.GetRopesLayer().Buffer[ropeElementIndex],
        oldCoords,
        newCoords);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
}

bool ModelController::EraseRopeAt(ShipSpaceCoordinates const & coords)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    assert(!mIsRopesLayerInEphemeralVisualization);

    //
    // Update model
    //

    bool const hasRemoved = InternalEraseRopeAt(coords);

    if (hasRemoved)
    {
        // Update visualization
        RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

        return true;
    }
    else
    {
        return false;
    }
}

void ModelController::EraseRopesRegion(ShipSpaceRect const & region)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    assert(!mIsRopesLayerInEphemeralVisualization);

    //
    // Update model
    //

    mModel.GetRopesLayer().Buffer.EraseRegion(region);

    // Update visualization
    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
}

void ModelController::RestoreRopesLayerRegionBackup(
    RopesLayerData && sourceLayerRegionBackup,
    ShipSpaceCoordinates const & targetOrigin)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    assert(!mIsRopesLayerInEphemeralVisualization);

    //
    // Restore model
    //

    mModel.GetRopesLayer().RestoreRegionBackup(
        std::move(sourceLayerRegionBackup),
        targetOrigin);

    //
    // Re-initialize layer analysis
    //

    InitializeRopesLayerAnalysis();

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
}

void ModelController::RestoreRopesLayer(std::unique_ptr<RopesLayerData> sourceLayer)
{
    assert(!mIsRopesLayerInEphemeralVisualization);

    //
    // Restore model
    //

    mModel.RestoreRopesLayer(std::move(sourceLayer));

    //
    // Re-initialize layer analysis
    //

    InitializeRopesLayerAnalysis();

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
}

void ModelController::AddRopeForEphemeralVisualization(
    ShipSpaceCoordinates const & startCoords,
    ShipSpaceCoordinates const & endCoords,
    StructuralMaterial const * material)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    //
    // Update model with just material - no analyses
    //

    AppendRope(
        startCoords,
        endCoords,
        material);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

    // Remember we are in temp visualization now
    mIsRopesLayerInEphemeralVisualization = true;
}

void ModelController::ModelController::MoveRopeEndpointForEphemeralVisualization(
    size_t ropeElementIndex,
    ShipSpaceCoordinates const & oldCoords,
    ShipSpaceCoordinates const & newCoords)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    //
    // Update model with jsut movement - no analyses
    //

    assert(ropeElementIndex < mModel.GetRopesLayer().Buffer.GetElementCount());

    MoveRopeEndpoint(
        mModel.GetRopesLayer().Buffer[ropeElementIndex],
        oldCoords,
        newCoords);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

    // Remember we are in temp visualization now
    mIsRopesLayerInEphemeralVisualization = true;
}

void ModelController::RestoreRopesLayerEphemeralVisualization(typename LayerTypeTraits<LayerType::Ropes>::buffer_type const & backupBuffer)
{
    assert(mModel.HasLayer(LayerType::Ropes));
    assert(mIsRopesLayerInEphemeralVisualization);

    //
    // Restore model, and nothing else
    //

    mModel.GetRopesLayer().Buffer = backupBuffer;

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());

    // Remember we are not anymore in temp visualization
    mIsRopesLayerInEphemeralVisualization = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture
////////////////////////////////////////////////////////////////////////////////////////////////////

TextureLayerData const & ModelController::GetTextureLayer() const
{
    assert(mModel.HasLayer(LayerType::Texture));

    return mModel.GetTextureLayer();
}

ShipSpaceRect ModelController::ImageRectToContainingShipSpaceRect(ImageRect const & imageRect) const
{
    vec2f const imageToShipFactor = GetTextureSpaceToShipSpaceFactor(GetTextureSize(), GetShipSize());

    ShipSpaceRect containingRect(
        ShipSpaceCoordinates(
            static_cast<int>(std::floor(static_cast<float>(imageRect.origin.x) * imageToShipFactor.x)),
            static_cast<int>(std::floor(static_cast<float>(imageRect.origin.y) * imageToShipFactor.y))),
        ShipSpaceSize(
            static_cast<int>(std::ceil(static_cast<float>(imageRect.size.width) * imageToShipFactor.x)),
            static_cast<int>(std::ceil(static_cast<float>(imageRect.size.height) * imageToShipFactor.y))));

    // Make sure not too wide
    auto const intersection = containingRect.MakeIntersectionWith(ShipSpaceRect(GetShipSize()));
    assert(intersection.has_value());
    return *intersection;
}

void ModelController::SetTextureLayer(
    TextureLayerData && textureLayer,
    std::optional<std::string> originalTextureArtCredits)
{
    mModel.SetTextureLayer(std::move(textureLayer));
    mModel.GetShipMetadata().ArtCredits = std::move(originalTextureArtCredits);

    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    RegisterDirtyVisualization<VisualizationType::TextureLayer>(GetWholeTextureRect());

    mIsTextureLayerInEphemeralVisualization = false;
}

void ModelController::RemoveTextureLayer()
{
    assert(mModel.HasLayer(LayerType::Texture));

    auto const oldWholeTextureRect = GetWholeTextureRect();

    mModel.RemoveTextureLayer();
    mModel.GetShipMetadata().ArtCredits.reset();

    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    RegisterDirtyVisualization<VisualizationType::TextureLayer>(oldWholeTextureRect);

    mIsTextureLayerInEphemeralVisualization = false;
}

std::unique_ptr<TextureLayerData> ModelController::CloneTextureLayer() const
{
    return mModel.CloneTextureLayer();
}

void ModelController::EraseTextureRegion(ImageRect const & region)
{
    assert(mModel.HasLayer(LayerType::Texture));

    assert(!mIsTextureLayerInEphemeralVisualization);

    assert(region.IsContainedInRect(ImageRect(mModel.GetTextureLayer().Buffer.Size)));

    //
    // Update model
    //

    auto & textureLayerBuffer = mModel.GetTextureLayer().Buffer;

    for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
    {
        for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
        {
            textureLayerBuffer[{x, y}].a = 0;
        }
    }

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(ImageRectToContainingShipSpaceRect(region));
    RegisterDirtyVisualization<VisualizationType::TextureLayer>(region);
}

std::optional<ImageRect> ModelController::TextureMagicWandEraseBackground(
    ImageCoordinates const & start,
    unsigned int tolerance,
    bool isAntiAlias,
    bool doContiguousOnly)
{
    assert(mModel.HasLayer(LayerType::Texture));

    assert(!mIsTextureLayerInEphemeralVisualization);

    //
    // Update model
    //

    std::optional<ImageRect> affectedRect = DoTextureMagicWandEraseBackground(
        start,
        tolerance,
        isAntiAlias,
        doContiguousOnly,
        mModel.GetTextureLayer());

    if (affectedRect.has_value())
    {
        //
        // Update visualization
        //

        RegisterDirtyVisualization<VisualizationType::Game>(ImageRectToContainingShipSpaceRect(*affectedRect));
        RegisterDirtyVisualization<VisualizationType::TextureLayer>(*affectedRect);
    }

    return affectedRect;
}

void ModelController::RestoreTextureLayerRegionBackup(
    TextureLayerData && sourceLayerRegionBackup,
    ImageCoordinates const & targetOrigin)
{
    assert(mModel.HasLayer(LayerType::Texture));

    assert(!mIsTextureLayerInEphemeralVisualization);

    //
    // Restore model
    //

    auto const affectedRegion = ImageRect(
        targetOrigin,
        sourceLayerRegionBackup.Buffer.Size);

    mModel.GetTextureLayer().RestoreRegionBackup(
        std::move(sourceLayerRegionBackup),
        targetOrigin);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(ImageRectToContainingShipSpaceRect(affectedRegion));
    RegisterDirtyVisualization<VisualizationType::TextureLayer>(affectedRegion);
}

void ModelController::RestoreTextureLayer(
    std::unique_ptr<TextureLayerData> textureLayer,
    std::optional<std::string> originalTextureArtCredits)
{
    assert(!mIsTextureLayerInEphemeralVisualization);

    //
    // Restore model
    //

    mModel.RestoreTextureLayer(std::move(textureLayer));
    mModel.GetShipMetadata().ArtCredits = std::move(originalTextureArtCredits);

    //
    // Update visualization
    //

    mGameVisualizationTexture.reset();
    mGameVisualizationAutoTexturizationTexture.release();
    RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    if (mModel.HasLayer(LayerType::Texture))
    {
        RegisterDirtyVisualization<VisualizationType::TextureLayer>(GetWholeTextureRect());
    }
    else
    {
        // We've just removed the texture layer; we rely now on texture viz mode being set to None
        // before we UpdateVisualizations(), causing the View texture to be remove
    }
}

void ModelController::TextureRegionEraseForEphemeralVisualization(ImageRect const & region)
{
    assert(mModel.HasLayer(LayerType::Texture));

    //
    // Update model
    //

    auto & textureLayerBuffer = mModel.GetTextureLayer().Buffer;

    for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
    {
        for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
        {
            textureLayerBuffer[{x, y}].a = 0;
        }
    }

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(ImageRectToContainingShipSpaceRect(region));
    RegisterDirtyVisualization<VisualizationType::TextureLayer>(region);

    // Remember we are in temp visualization now
    mIsTextureLayerInEphemeralVisualization = true;
}

void ModelController::RestoreTextureLayerRegionEphemeralVisualization(
    typename LayerTypeTraits<LayerType::Texture>::buffer_type const & backupBuffer,
    ImageRect const & backupBufferRegion,
    ImageCoordinates const & targetOrigin)
{
    assert(mIsTextureLayerInEphemeralVisualization);

    DoTextureRegionBufferPaste(
        backupBuffer,
        backupBufferRegion,
        ImageRect(targetOrigin, backupBufferRegion.size),
        targetOrigin,
        false);

    // Remember we are not anymore in temp visualization
    mIsTextureLayerInEphemeralVisualization = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Visualizations
////////////////////////////////////////////////////////////////////////////////////////////////////

void ModelController::SetGameVisualizationMode(GameVisualizationModeType mode)
{
    if (mode == mGameVisualizationMode)
    {
        // Nop
        return;
    }

    if (mode != GameVisualizationModeType::None)
    {
        if (mode != GameVisualizationModeType::AutoTexturizationMode)
        {
            mGameVisualizationAutoTexturizationTexture.reset();
        }

        mGameVisualizationMode = mode;

        RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    }
    else
    {
        // Shutdown game visualization
        mGameVisualizationMode = GameVisualizationModeType::None;
        mGameVisualizationAutoTexturizationTexture.reset();
        mGameVisualizationTexture.reset();
    }
}

void ModelController::ForceWholeGameVisualizationRefresh()
{
    if (mGameVisualizationMode != GameVisualizationModeType::None)
    {
        RegisterDirtyVisualization<VisualizationType::Game>(GetWholeShipRect());
    }
}

void ModelController::SetStructuralLayerVisualizationMode(StructuralLayerVisualizationModeType mode)
{
    if (mode == mStructuralLayerVisualizationMode)
    {
        // Nop
        return;
    }

    if (mode != StructuralLayerVisualizationModeType::None)
    {
        mStructuralLayerVisualizationMode = mode;

        RegisterDirtyVisualization<VisualizationType::StructuralLayer>(GetWholeShipRect());
    }
    else
    {
        // Shutdown structural visualization
        mStructuralLayerVisualizationMode = StructuralLayerVisualizationModeType::None;
        mStructuralLayerVisualizationTexture.reset();
    }
}

void ModelController::SetElectricalLayerVisualizationMode(ElectricalLayerVisualizationModeType mode)
{
    if (mode == mElectricalLayerVisualizationMode)
    {
        // Nop
        return;
    }

    if (mode != ElectricalLayerVisualizationModeType::None)
    {
        mElectricalLayerVisualizationMode = mode;

        RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(GetWholeShipRect());
    }
    else
    {
        // Shutdown electrical visualization
        mElectricalLayerVisualizationMode = ElectricalLayerVisualizationModeType::None;
        mElectricalLayerVisualizationTexture.reset();
    }
}

void ModelController::SetRopesLayerVisualizationMode(RopesLayerVisualizationModeType mode)
{
    if (mode == mRopesLayerVisualizationMode)
    {
        // Nop
        return;
    }

    if (mode != RopesLayerVisualizationModeType::None)
    {
        mRopesLayerVisualizationMode = mode;

        RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
    }
    else
    {
        // Shutdown ropes visualization
        mRopesLayerVisualizationMode = RopesLayerVisualizationModeType::None;
    }
}

void ModelController::SetTextureLayerVisualizationMode(TextureLayerVisualizationModeType mode)
{
    if (mode == mTextureLayerVisualizationMode)
    {
        // Nop
        return;
    }

    if (mode != TextureLayerVisualizationModeType::None)
    {
        mTextureLayerVisualizationMode = mode;

        assert(mModel.HasLayer(LayerType::Texture));

        RegisterDirtyVisualization<VisualizationType::TextureLayer>(GetWholeTextureRect());
    }
    else
    {
        // Shutdown texture visualization
        mTextureLayerVisualizationMode = TextureLayerVisualizationModeType::None;
    }
}

void ModelController::UpdateVisualizations(View & view)
{
    //
    // Update and upload visualizations that are dirty, and
    // remove visualizations that are not needed
    //

    // Game

    if (mGameVisualizationMode != GameVisualizationModeType::None)
    {
        if (!mGameVisualizationTexture)
        {
            // Initialize game visualization texture
            mGameVisualizationTextureMagnificationFactor = ShipTexturizer::CalculateHighDefinitionTextureMagnificationFactor(mModel.GetShipSize());
            ImageSize const textureSize = ImageSize(
                mModel.GetShipSize().width * mGameVisualizationTextureMagnificationFactor,
                mModel.GetShipSize().height * mGameVisualizationTextureMagnificationFactor);

            mGameVisualizationTexture = std::make_unique<RgbaImageData>(textureSize);
        }

        if (mGameVisualizationMode == GameVisualizationModeType::AutoTexturizationMode && !mGameVisualizationAutoTexturizationTexture)
        {
            // Initialize auto-texturization texture
            mGameVisualizationAutoTexturizationTexture = std::make_unique<RgbaImageData>(mGameVisualizationTexture->Size);
        }

        if (mDirtyGameVisualizationRegion.has_value())
        {
            // Update visualization
            ImageRect const dirtyTextureRegion = UpdateGameVisualization(*mDirtyGameVisualizationRegion);

            // Upload visualization
            if (dirtyTextureRegion != ImageRect(mGameVisualizationTexture->Size))
            {
                //
                // For better performance, we only upload the dirty sub-texture
                //

                auto subTexture = RgbaImageData(dirtyTextureRegion.size);
                subTexture.BlitFromRegion(
                    *mGameVisualizationTexture,
                    dirtyTextureRegion,
                    { 0, 0 });

                view.UpdateGameVisualization(
                    subTexture,
                    dirtyTextureRegion.origin);
            }
            else
            {
                // Upload whole texture
                view.UploadGameVisualization(*mGameVisualizationTexture);
            }
        }
    }
    else
    {
        assert(!mGameVisualizationTexture);

        if (view.HasGameVisualization())
        {
            view.RemoveGameVisualization();
        }
    }

    mDirtyGameVisualizationRegion.reset();

    // Structural

    if (mStructuralLayerVisualizationMode != StructuralLayerVisualizationModeType::None)
    {
        if (!mStructuralLayerVisualizationTexture)
        {
            // Initialize structural visualization
            mStructuralLayerVisualizationTexture = std::make_unique<RgbaImageData>(ImageSize(mModel.GetShipSize().width, mModel.GetShipSize().height));
        }

        if (mDirtyStructuralLayerVisualizationRegion.has_value())
        {
            // Refresh viz mode
            if (mStructuralLayerVisualizationMode == StructuralLayerVisualizationModeType::MeshMode)
            {
                view.SetStructuralLayerVisualizationDrawMode(View::StructuralLayerVisualizationDrawMode::MeshMode);
            }
            else
            {
                assert(mStructuralLayerVisualizationMode == StructuralLayerVisualizationModeType::PixelMode);
                view.SetStructuralLayerVisualizationDrawMode(View::StructuralLayerVisualizationDrawMode::PixelMode);
            }

            // Update visualization
            ImageRect const dirtyTextureRegion = UpdateStructuralLayerVisualization(*mDirtyStructuralLayerVisualizationRegion);

            // Upload visualization
            if (dirtyTextureRegion != ImageRect(mStructuralLayerVisualizationTexture->Size))
            {
                //
                // For better performance, we only upload the dirty sub-texture
                //

                auto subTexture = RgbaImageData(dirtyTextureRegion.size);
                subTexture.BlitFromRegion(
                    *mStructuralLayerVisualizationTexture,
                    dirtyTextureRegion,
                    { 0, 0 });

                view.UpdateStructuralLayerVisualization(
                    subTexture,
                    dirtyTextureRegion.origin);
            }
            else
            {
                // Upload whole texture
                view.UploadStructuralLayerVisualization(*mStructuralLayerVisualizationTexture);
            }
        }
    }
    else
    {
        assert(!mStructuralLayerVisualizationTexture);

        if (view.HasStructuralLayerVisualization())
        {
            view.RemoveStructuralLayerVisualization();
        }
    }

    mDirtyStructuralLayerVisualizationRegion.reset();

    // Electrical

    if (mElectricalLayerVisualizationMode != ElectricalLayerVisualizationModeType::None)
    {
        if (!mElectricalLayerVisualizationTexture)
        {
            // Initialize electrical visualization
            mElectricalLayerVisualizationTexture = std::make_unique<RgbaImageData>(mModel.GetShipSize().width, mModel.GetShipSize().height);
        }

        if (mDirtyElectricalLayerVisualizationRegion.has_value())
        {
            // Update visualization
            UpdateElectricalLayerVisualization(*mDirtyElectricalLayerVisualizationRegion);

            // Upload visualization
            view.UploadElectricalLayerVisualization(*mElectricalLayerVisualizationTexture);
        }
    }
    else
    {
        assert(!mElectricalLayerVisualizationTexture);

        if (view.HasElectricalLayerVisualization())
        {
            view.RemoveElectricalLayerVisualization();
        }
    }

    mDirtyElectricalLayerVisualizationRegion.reset();

    // Ropes

    if (mRopesLayerVisualizationMode != RopesLayerVisualizationModeType::None)
    {
        assert(mModel.HasLayer(LayerType::Ropes));

        if (mDirtyRopesLayerVisualizationRegion.has_value())
        {
            // Update visualization
            UpdateRopesLayerVisualization(); // Dirty region not needed in this implementation

            // Upload visualization
            view.UploadRopesLayerVisualization(mModel.GetRopesLayer().Buffer);
        }
    }
    else
    {
        if (view.HasRopesLayerVisualization())
        {
            view.RemoveRopesLayerVisualization();
        }
    }

    mDirtyRopesLayerVisualizationRegion.reset();

    // Texture

    if (mTextureLayerVisualizationMode != TextureLayerVisualizationModeType::None)
    {
        assert(mModel.HasLayer(LayerType::Texture));

        if (mDirtyTextureLayerVisualizationRegion.has_value())
        {
            // Update visualization
            UpdateTextureLayerVisualization(); // Dirty region not needed for updating viz in this implementation

            // Upload visualization
            if (*mDirtyTextureLayerVisualizationRegion != ImageRect(mModel.GetTextureLayer().Buffer.Size))
            {
                //
                // For better performance, we only upload the dirty sub-texture
                //

                auto subTexture = RgbaImageData(mDirtyTextureLayerVisualizationRegion->size);
                subTexture.BlitFromRegion(
                    mModel.GetTextureLayer().Buffer,
                    *mDirtyTextureLayerVisualizationRegion,
                    { 0, 0 });

                view.UpdateTextureLayerVisualization(
                    subTexture,
                    mDirtyTextureLayerVisualizationRegion->origin);
            }
            else
            {
                // Upload whole texture
                view.UploadTextureLayerVisualization(mModel.GetTextureLayer().Buffer);
            }
        }
    }
    else
    {
        if (view.HasTextureLayerVisualization())
        {
            view.RemoveTextureLayerVisualization();
        }
    }

    mDirtyTextureLayerVisualizationRegion.reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void ModelController::InitializeStructuralLayerAnalysis()
{
    mMassParticleCount = 0;
    mTotalMass = 0.0f;
    mCenterOfMassSum = vec2f::zero();

    auto const & structuralLayerBuffer = mModel.GetStructuralLayer().Buffer;

    for (int y = 0; y < structuralLayerBuffer.Size.height; ++y)
    {
        for (int x = 0; x < structuralLayerBuffer.Size.width; ++x)
        {
            ShipSpaceCoordinates const coords{ x, y };
            if (structuralLayerBuffer[coords].Material != nullptr)
            {
                auto const mass = structuralLayerBuffer[coords].Material->GetMass();

                ++mMassParticleCount;
                mTotalMass += mass;
                mCenterOfMassSum += coords.ToFloat() * mass;
            }
        }
    }
}

void ModelController::InitializeElectricalLayerAnalysis()
{
    // Reset factory
    mInstancedElectricalElementSet.Reset();

    // Reset particle count
    mElectricalParticleCount = 0;

    if (mModel.HasLayer(LayerType::Electrical))
    {
        // Register existing instance indices with factory, and initialize running analysis
        auto const & electricalLayerBuffer = mModel.GetElectricalLayer().Buffer;
        for (size_t i = 0; i < electricalLayerBuffer.Size.GetLinearSize(); ++i)
        {
            if (electricalLayerBuffer.Data[i].Material != nullptr)
            {
                ++mElectricalParticleCount;

                if (electricalLayerBuffer.Data[i].Material->IsInstanced)
                {
                    assert(electricalLayerBuffer.Data[i].InstanceIndex != NoneElectricalElementInstanceIndex);
                    mInstancedElectricalElementSet.Register(electricalLayerBuffer.Data[i].InstanceIndex, electricalLayerBuffer.Data[i].Material);
                }
            }
        }
    }
}

void ModelController::InitializeRopesLayerAnalysis()
{
    // Nop
}

void ModelController::WriteParticle(
    ShipSpaceCoordinates const & coords,
    StructuralMaterial const * material)
{
    // Write particle and maintain analysis

    auto & structuralLayerBuffer = mModel.GetStructuralLayer().Buffer;

    if (structuralLayerBuffer[coords].Material != nullptr)
    {
        auto const mass = structuralLayerBuffer[coords].Material->GetMass();

        assert(mMassParticleCount > 0);
        --mMassParticleCount;
        if (mMassParticleCount == 0)
        {
            mTotalMass = 0.0f;
        }
        else
        {
            mTotalMass -= mass;
        }
        mCenterOfMassSum -= coords.ToFloat() * mass;
    }

    structuralLayerBuffer[coords] = StructuralElement(material);

    if (material != nullptr)
    {
        auto const mass = material->GetMass();

        ++mMassParticleCount;
        mTotalMass += mass;
        mCenterOfMassSum += coords.ToFloat() * mass;
    }
}

void ModelController::WriteParticle(
    ShipSpaceCoordinates const & coords,
    ElectricalMaterial const * material)
{
    auto & electricalLayerBuffer = mModel.GetElectricalLayer().Buffer;

    auto const & oldElement = electricalLayerBuffer[coords];

    // Decide instance index and maintain panel
    ElectricalElementInstanceIndex instanceIndex;
    if (oldElement.Material == nullptr
        || !oldElement.Material->IsInstanced)
    {
        if (material != nullptr
            && material->IsInstanced)
        {
            // New instanced element...

            // ...new instance index
            instanceIndex = mInstancedElectricalElementSet.Add(material);
        }
        else
        {
            // None instanced...

            // ...keep it none
            instanceIndex = NoneElectricalElementInstanceIndex;
        }
    }
    else
    {
        assert(oldElement.Material->IsInstanced);
        assert(oldElement.InstanceIndex != NoneElectricalElementInstanceIndex);

        if (material == nullptr
            || !material->IsInstanced)
        {
            // Old instanced, new one not...

            // ...disappeared instance index
            mInstancedElectricalElementSet.Remove(oldElement.InstanceIndex);
            instanceIndex = NoneElectricalElementInstanceIndex;

            // Remove from panel (if there)
            mModel.GetElectricalLayer().Panel.TryRemove(oldElement.InstanceIndex);
        }
        else
        {
            // Both instanced...

            // ...keep old instanceIndex
            instanceIndex = oldElement.InstanceIndex;
        }
    }

    // Update electrical element count
    if (material != nullptr)
    {
        if (oldElement.Material == nullptr)
        {
            ++mElectricalParticleCount;
        }
    }
    else
    {
        if (oldElement.Material != nullptr)
        {
            assert(mElectricalParticleCount > 0);
            --mElectricalParticleCount;
        }
    }

    // Store
    electricalLayerBuffer[coords] = ElectricalElement(material, instanceIndex);
}

void ModelController::AppendRope(
    ShipSpaceCoordinates const & startCoords,
    ShipSpaceCoordinates const & endCoords,
    StructuralMaterial const * material)
{
    assert(material != nullptr);

    mModel.GetRopesLayer().Buffer.EmplaceBack(
        startCoords,
        endCoords,
        material,
        material->RenderColor);
}

void ModelController::MoveRopeEndpoint(
    RopeElement & ropeElement,
    ShipSpaceCoordinates const & oldCoords,
    ShipSpaceCoordinates const & newCoords)
{
    if (ropeElement.StartCoords == oldCoords)
    {
        ropeElement.StartCoords = newCoords;
    }
    else
    {
        assert(ropeElement.EndCoords == oldCoords);
        ropeElement.EndCoords = newCoords;
    }
}

bool ModelController::InternalEraseRopeAt(ShipSpaceCoordinates const & coords)
{
    auto const srchIt = std::find_if(
        mModel.GetRopesLayer().Buffer.cbegin(),
        mModel.GetRopesLayer().Buffer.cend(),
        [&coords](auto const & e)
        {
            return e.StartCoords == coords
                || e.EndCoords == coords;
        });

    if (srchIt != mModel.GetRopesLayer().Buffer.cend())
    {
        // Remove
        mModel.GetRopesLayer().Buffer.Erase(srchIt);

        return true;
    }
    else
    {
        return false;
    }
}

template<LayerType TLayer>
std::optional<ShipSpaceRect> ModelController::DoFlood(
    ShipSpaceCoordinates const & start,
    typename LayerTypeTraits<TLayer>::material_type const * material,
    bool doContiguousOnly,
    typename LayerTypeTraits<TLayer>::layer_data_type const & layer)
{
    assert(start.IsInRect(GetWholeShipRect()));

    // Pick material to flood
    auto const * const startMaterial = layer.Buffer[start].Material;
    if (material == startMaterial)
    {
        // Nop
        return std::nullopt;
    }

    ShipSpaceSize const shipSize = mModel.GetShipSize();

    if (doContiguousOnly)
    {
        //
        // Flood from point
        //

        //
        // Init visit from this point
        //

        WriteParticle(start, material);
        ShipSpaceRect affectedRect(start);

        std::queue<ShipSpaceCoordinates> pointsToPropagateFrom;
        pointsToPropagateFrom.push(start);

        //
        // Propagate
        //

        auto const checkPropagateToNeighbor = [&](ShipSpaceCoordinates neighborCoords)
        {
            if (neighborCoords.IsInSize(shipSize) && layer.Buffer[neighborCoords].Material == startMaterial)
            {
                // Visit point
                WriteParticle(neighborCoords, material);
                affectedRect.UnionWith(neighborCoords);

                // Propagate from point
                pointsToPropagateFrom.push(neighborCoords);
            }
        };

        while (!pointsToPropagateFrom.empty())
        {
            // Pop point that we have to propagate from
            auto const currentPoint = pointsToPropagateFrom.front();
            pointsToPropagateFrom.pop();

            // Push neighbors
            checkPropagateToNeighbor({ currentPoint.x - 1, currentPoint.y });
            checkPropagateToNeighbor({ currentPoint.x + 1, currentPoint.y });
            checkPropagateToNeighbor({ currentPoint.x, currentPoint.y - 1 });
            checkPropagateToNeighbor({ currentPoint.x, currentPoint.y + 1 });
        }

        return affectedRect;
    }
    else
    {
        //
        // Replace material
        //

        std::optional<ShipSpaceRect> affectedRect;

        for (int y = 0; y < shipSize.height; ++y)
        {
            for (int x = 0; x < shipSize.width; ++x)
            {
                ShipSpaceCoordinates const coords(x, y);

                if (layer.Buffer[coords].Material == startMaterial)
                {
                    WriteParticle(coords, material);

                    if (!affectedRect.has_value())
                    {
                        affectedRect = ShipSpaceRect(coords);
                    }
                    else
                    {
                        affectedRect->UnionWith(coords);
                    }
                }
            }
        }

        return affectedRect;
    }
}

std::optional<ImageRect> ModelController::DoTextureMagicWandEraseBackground(
    ImageCoordinates const & start,
    unsigned int tolerance,
    bool isAntiAlias,
    bool doContiguousOnly,
    TextureLayerData & layer)
{
    //
    // For the purposes of this operation, a pixel might exist or not;
    // it exists if and only if its alpha is not zero.
    //

    auto const textureSize = layer.Buffer.Size;

    assert(start.IsInSize(textureSize));

    // Get starting color
    rgbaColor const seedColorRgb = layer.Buffer[start];
    if (seedColorRgb.a == 0)
    {
        // Starting pixel does not exist
        return std::nullopt;
    }

    vec3f const seedColor = seedColorRgb.toVec3f();

    // Our distance function - from https://www.photoshopgurus.com/forum/threads/tolerance.52555/page-2
    auto const distanceFromSeed = [seedColor](vec3f const & sampleColor) -> float
    {
        vec3f const deltaColor = (sampleColor - seedColor).abs();

        return std::max({
            deltaColor.x,
            deltaColor.y,
            deltaColor.z,
            std::abs(deltaColor.x - deltaColor.y),
            std::abs(deltaColor.y - deltaColor.z),
            std::abs(deltaColor.z - deltaColor.x) });
    };

    // Transform tolerance into max distance (included)
    float const maxColorDistance = static_cast<float>(tolerance) / 100.0f;

    // Save original alpha mask for neighbors
    Buffer2D<typename rgbaColor::data_type, ImageTag> originalAlphaMask = layer.Buffer.Transform<typename rgbaColor::data_type>(
        [](rgbaColor const & color) -> typename rgbaColor::data_type
        {
            return color.a;
        });

    // Initialize affected region
    ImageRect affectedRegion(start); // We're sure we'll erase the start pixel

    // Anti-alias functor
    auto const doAntiAliasNeighbor = [&](ImageCoordinates const & neighborCoordinates) -> void
    {
        layer.Buffer[neighborCoordinates].a = originalAlphaMask[neighborCoordinates] / 3;
        affectedRegion.UnionWith(neighborCoordinates);
    };

    if (doContiguousOnly)
    {
        //
        // Flood from starting point
        //

        std::queue<ImageCoordinates> pixelsToPropagateFrom;

        // Erase this pixel
        layer.Buffer[start].a = 0;
        affectedRegion.UnionWith(start);

        // Visit from here
        pixelsToPropagateFrom.push(start);

        while (!pixelsToPropagateFrom.empty())
        {
            auto const sourceCoords = pixelsToPropagateFrom.front();
            pixelsToPropagateFrom.pop();

            // Check neighbors
            for (int yn = sourceCoords.y - 1; yn <= sourceCoords.y + 1; ++yn)
            {
                for (int xn = sourceCoords.x - 1; xn <= sourceCoords.x + 1; ++xn)
                {
                    ImageCoordinates const neighborCoordinates{ xn, yn };
                    if (neighborCoordinates.IsInSize(textureSize)
                        && layer.Buffer[neighborCoordinates].a != 0)
                    {
                        // Check distance
                        if (distanceFromSeed(layer.Buffer[neighborCoordinates].toVec3f()) <= maxColorDistance)
                        {
                            // Erase this pixel
                            layer.Buffer[neighborCoordinates].a = 0;
                            affectedRegion.UnionWith(neighborCoordinates);

                            // Continue visit from here
                            pixelsToPropagateFrom.push(neighborCoordinates);
                        }
                        else if (isAntiAlias)
                        {
                            // Anti-alias this neighbor
                            doAntiAliasNeighbor(neighborCoordinates);
                        }
                    }
                }
            }
        }
    }
    else
    {
        //
        // Color substitution
        //

        for (int y = 0; y < textureSize.height; ++y)
        {
            for (int x = 0; x < textureSize.width; ++x)
            {
                ImageCoordinates const sampleCoordinates{ x, y };

                if (layer.Buffer[sampleCoordinates].a != 0
                    && distanceFromSeed(layer.Buffer[sampleCoordinates].toVec3f()) <= maxColorDistance)
                {
                    // Erase
                    layer.Buffer[sampleCoordinates].a = 0;
                    affectedRegion.UnionWith(sampleCoordinates);

                    if (isAntiAlias)
                    {
                        //
                        // Do anti-aliasing on neighboring, too-distant pixels
                        //

                        for (int yn = y - 1; yn <= y + 1; ++yn)
                        {
                            for (int xn = x - 1; xn <= x + 1; ++xn)
                            {
                                ImageCoordinates const neighborCoordinates{ xn, yn };
                                if (neighborCoordinates.IsInSize(textureSize)
                                    && layer.Buffer[neighborCoordinates].a != 0
                                    && distanceFromSeed(layer.Buffer[neighborCoordinates].toVec3f()) > maxColorDistance)
                                {
                                    doAntiAliasNeighbor(neighborCoordinates);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return affectedRegion;
}

GenericUndoPayload ModelController::MakeGenericUndoPayload(
    std::optional<ShipSpaceRect> const & region,
    bool doStructuralLayer,
    bool doElectricalLayer,
    bool doRopesLayer,
    bool doTextureLayer) const
{
    ShipSpaceRect actualRegion = region.value_or(GetWholeShipRect());

    // The requested region is entirely within the ship
    assert(actualRegion.IsContainedInRect(GetWholeShipRect()));

    std::optional<StructuralLayerData> structuralLayerRegionBackup;
    std::optional<ElectricalLayerData> electricalLayerRegionBackup;
    std::optional<RopesLayerData> ropesLayerRegionBackup;
    std::optional<TextureLayerData> textureLayerRegionBackup;

    if (doStructuralLayer)
    {
        structuralLayerRegionBackup = mModel.GetStructuralLayer().MakeRegionBackup(actualRegion);
    }

    if (doElectricalLayer)
    {
        electricalLayerRegionBackup = mModel.GetElectricalLayer().MakeRegionBackup(actualRegion);
    }

    if (doRopesLayer)
    {
        ropesLayerRegionBackup = mModel.GetRopesLayer().MakeRegionBackup(actualRegion);
    }

    if (doTextureLayer)
    {
        // The requested region - if any - is entirely within the ship
        assert(!region.has_value() || ShipSpaceToTextureSpace(*region).IsContainedInRect(GetWholeTextureRect()));

        ImageRect actualTextureRegion = region.has_value()
            ? ShipSpaceToTextureSpace(*region)
            : GetWholeTextureRect();

        textureLayerRegionBackup = mModel.GetTextureLayer().MakeRegionBackup(actualTextureRegion);
    }

    return GenericUndoPayload(
        actualRegion.origin,
        std::move(structuralLayerRegionBackup),
        std::move(electricalLayerRegionBackup),
        std::move(ropesLayerRegionBackup),
        std::move(textureLayerRegionBackup));
}

GenericEphemeralVisualizationRestorePayload ModelController::MakeGenericEphemeralVisualizationRestorePayload(
    ShipSpaceRect const & region,
    bool doStructuralLayer,
    bool doElectricalLayer,
    bool doRopesLayer,
    bool doTextureLayer) const
{
    // The requested region is entirely within the ship
    assert(region.IsContainedInRect(GetWholeShipRect()));

    std::optional<typename LayerTypeTraits<LayerType::Structural>::buffer_type> structuralLayerBufferRegion;
    std::optional<typename LayerTypeTraits<LayerType::Electrical>::buffer_type> electricalLayerBufferRegion;
    std::optional<typename LayerTypeTraits<LayerType::Ropes>::buffer_type> ropesLayerBuffer; // Whole buffer
    std::optional<typename LayerTypeTraits<LayerType::Texture>::buffer_type> textureLayerBufferRegion;

    if (doStructuralLayer)
    {
        structuralLayerBufferRegion = mModel.GetStructuralLayer().Buffer.CloneRegion(region);
    }

    if (doElectricalLayer)
    {
        electricalLayerBufferRegion = mModel.GetElectricalLayer().Buffer.CloneRegion(region);
    }

    if (doRopesLayer)
    {
        ropesLayerBuffer = mModel.GetRopesLayer().Buffer.Clone();
    }

    if (doTextureLayer)
    {
        // The requested region is entirely within the ship
        assert(ShipSpaceToTextureSpace(region).IsContainedInRect(GetWholeTextureRect()));

        textureLayerBufferRegion = mModel.GetTextureLayer().Buffer.CloneRegion(ShipSpaceToTextureSpace(region));
    }

    return GenericEphemeralVisualizationRestorePayload(
        region.origin,
        std::move(structuralLayerBufferRegion),
        std::move(electricalLayerBufferRegion),
        std::move(ropesLayerBuffer),
        std::move(textureLayerBufferRegion));
}

void ModelController::DoStructuralRegionBufferPaste(
    typename LayerTypeTraits<LayerType::Structural>::buffer_type const & sourceBuffer,
    ShipSpaceRect const & sourceRegion,
    ShipSpaceCoordinates const & targetCoordinates,
    bool isTransparent)
{
    assert(mModel.HasLayer(LayerType::Structural));

    // Rect in the ship that will be affected by this operation
    ShipSpaceRect const affectedRegion(targetCoordinates, sourceRegion.size);

    // The affected region is entirely contained in this ship
    assert(affectedRegion.IsContainedInRect(GetWholeShipRect()));

    // The source region is entirely contained in the source buffer
    assert(sourceRegion.IsContainedInRect(ShipSpaceRect(sourceBuffer.Size)));

    auto const elementOperator = isTransparent
        ? [](StructuralElement const & src, StructuralElement const & dst) -> StructuralElement const &
        {
            return src.Material
                ? src
                : dst;
        }
        : [](StructuralElement const & src, StructuralElement const &) -> StructuralElement const &
        {
            return src;
        };

    mModel.GetStructuralLayer().Buffer.BlitFromRegion(
        sourceBuffer,
        sourceRegion,
        targetCoordinates,
        elementOperator);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(affectedRegion);
    RegisterDirtyVisualization<VisualizationType::StructuralLayer>(affectedRegion);
}

void ModelController::DoElectricalRegionBufferPaste(
    typename LayerTypeTraits<LayerType::Electrical>::buffer_type const & sourceBuffer,
    ShipSpaceRect const & sourceRegion,
    ShipSpaceCoordinates const & targetCoordinates,
    std::function<ElectricalElement const &(ElectricalElement const &, ElectricalElement const &)> elementOperator)
{
    assert(mModel.HasLayer(LayerType::Electrical));

    // Rect in the ship that will be affected by this operation
    ShipSpaceRect const affectedRegion(targetCoordinates, sourceRegion.size);

    // The affected region is entirely contained in this ship
    assert(affectedRegion.IsContainedInRect(GetWholeShipRect()));

    // The source region is entirely contained in the source buffer
    assert(sourceRegion.IsContainedInRect(ShipSpaceRect(sourceBuffer.Size)));

    mModel.GetElectricalLayer().Buffer.BlitFromRegion(
        sourceBuffer,
        sourceRegion,
        targetCoordinates,
        elementOperator);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::ElectricalLayer>(affectedRegion);
}

void ModelController::DoRopesRegionBufferPaste(
    typename LayerTypeTraits<LayerType::Ropes>::buffer_type const & sourceBuffer,
    ShipSpaceRect const & sourceRegion,
    ShipSpaceCoordinates const & targetCoordinates,
    bool isTransparent)
{
    assert(mModel.HasLayer(LayerType::Ropes));

    // Rect in the ship that will be affected by this operation
    ShipSpaceRect const affectedRegion(targetCoordinates, sourceRegion.size);

    // The affected region is entirely contained in this ship
    assert(affectedRegion.IsContainedInRect(GetWholeShipRect()));

    // The source region is entirely contained in the source buffer
    assert(sourceRegion.IsContainedInRect(ShipSpaceRect(sourceBuffer.GetSize())));

    mModel.GetRopesLayer().Buffer.BlitFromRegion(
        sourceBuffer,
        sourceRegion,
        targetCoordinates,
        isTransparent);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::RopesLayer>(GetWholeShipRect());
}

void ModelController::DoTextureRegionBufferPaste(
    typename LayerTypeTraits<LayerType::Texture>::buffer_type const & sourceBuffer,
    ImageRect const & sourceRegion,
    ImageRect const & targetRegion,
    ImageCoordinates const & targetCoordinates,
    bool isTransparent)
{
    assert(mModel.HasLayer(LayerType::Texture));

    // The source region is entirely contained in the source buffer
    assert(sourceRegion.IsContainedInRect(ImageRect(sourceBuffer.Size)));

    // The target region is entirely contained in this ship
    assert(targetRegion.IsContainedInRect(GetWholeTextureRect()));

    auto const elementOperator = isTransparent
        ? [](rgbaColor const & src, rgbaColor const & dst) -> rgbaColor
        {
            return dst.blend(src);
        }
        : [](rgbaColor const & src, rgbaColor const &) -> rgbaColor
        {
            return src;
        };

    mModel.GetTextureLayer().Buffer.BlitFromRegion(
        sourceBuffer,
        sourceRegion,
        targetCoordinates,
        elementOperator);

    //
    // Update visualization
    //

    RegisterDirtyVisualization<VisualizationType::Game>(ImageRectToContainingShipSpaceRect(targetRegion)); // In excess
    RegisterDirtyVisualization<VisualizationType::TextureLayer>(targetRegion);
}

//////////////////////////////////////////////////////////////////////////////////////////////

template<VisualizationType TVisualization, typename TRect>
void ModelController::RegisterDirtyVisualization(TRect const & region)
{
    if constexpr (TVisualization == VisualizationType::Game)
    {
        if (!mDirtyGameVisualizationRegion.has_value())
        {
            mDirtyGameVisualizationRegion = region;
        }
        else
        {
            mDirtyGameVisualizationRegion->UnionWith(region);
        }
    }
    else if constexpr (TVisualization == VisualizationType::StructuralLayer)
    {
        if (!mDirtyStructuralLayerVisualizationRegion.has_value())
        {
            mDirtyStructuralLayerVisualizationRegion = region;
        }
        else
        {
            mDirtyStructuralLayerVisualizationRegion->UnionWith(region);
        }
    }
    else if constexpr (TVisualization == VisualizationType::ElectricalLayer)
    {
        if (!mDirtyElectricalLayerVisualizationRegion.has_value())
        {
            mDirtyElectricalLayerVisualizationRegion = region;
        }
        else
        {
            mDirtyElectricalLayerVisualizationRegion->UnionWith(region);
        }
    }
    else if constexpr (TVisualization == VisualizationType::RopesLayer)
    {
        if (!mDirtyRopesLayerVisualizationRegion.has_value())
        {
            mDirtyRopesLayerVisualizationRegion = region;
        }
        else
        {
            mDirtyRopesLayerVisualizationRegion->UnionWith(region);
        }
    }
    else
    {
        static_assert(TVisualization == VisualizationType::TextureLayer);

        if (!mDirtyTextureLayerVisualizationRegion.has_value())
        {
            mDirtyTextureLayerVisualizationRegion = region;
        }
        else
        {
            mDirtyTextureLayerVisualizationRegion->UnionWith(region);
        }
    }
}

ImageRect ModelController::UpdateGameVisualization(ShipSpaceRect const & region)
{
    //
    // 1. Prepare source of triangularized rendering
    //

    RgbaImageData const * sourceTexture = nullptr;

    if (mGameVisualizationMode == GameVisualizationModeType::AutoTexturizationMode)
    {
        assert(mModel.HasLayer(LayerType::Structural));
        assert(mGameVisualizationAutoTexturizationTexture);

        ShipAutoTexturizationSettings settings = mModel.GetShipAutoTexturizationSettings().value_or(ShipAutoTexturizationSettings());

        mShipTexturizer.AutoTexturizeInto(
            mModel.GetStructuralLayer(),
            region,
            *mGameVisualizationAutoTexturizationTexture,
            mGameVisualizationTextureMagnificationFactor,
            settings);

        sourceTexture = mGameVisualizationAutoTexturizationTexture.get();
    }
    else
    {
        assert(mGameVisualizationMode == GameVisualizationModeType::TextureMode);

        assert(mModel.HasLayer(LayerType::Structural));
        assert(mModel.HasLayer(LayerType::Texture));

        sourceTexture = &mModel.GetTextureLayer().Buffer;
    }

    assert(sourceTexture != nullptr);

    //
    // 2. Do triangularized rendering
    //

    // Given that texturization looks at x+1 and y+1, we enlarge the region down and to the left
    ShipSpaceRect effectiveRegion = region;
    if (effectiveRegion.origin.x > 0)
    {
        effectiveRegion.origin.x -= 1;
        effectiveRegion.size.width += 1;
    }

    if (effectiveRegion.origin.y > 0)
    {
        effectiveRegion.origin.y -= 1;
        effectiveRegion.size.height += 1;
    }

    assert(mGameVisualizationTexture);

    mShipTexturizer.RenderShipInto(
        mModel.GetStructuralLayer(),
        effectiveRegion,
        *sourceTexture,
        *mGameVisualizationTexture,
        mGameVisualizationTextureMagnificationFactor);

    //
    // 3. Return dirty image region
    //

    return ImageRect(
        ImageCoordinates(
            effectiveRegion.origin.x * mGameVisualizationTextureMagnificationFactor,
            effectiveRegion.origin.y * mGameVisualizationTextureMagnificationFactor),
        ImageSize(
            effectiveRegion.size.width * mGameVisualizationTextureMagnificationFactor,
            effectiveRegion.size.height * mGameVisualizationTextureMagnificationFactor));
}

ImageRect ModelController::UpdateStructuralLayerVisualization(ShipSpaceRect const & region)
{
    switch (mStructuralLayerVisualizationMode)
    {
        case StructuralLayerVisualizationModeType::MeshMode:
        case StructuralLayerVisualizationModeType::PixelMode:
        {
            assert(mStructuralLayerVisualizationTexture);

            RenderStructureInto(
                region,
                *mStructuralLayerVisualizationTexture);

            break;
        }

        case StructuralLayerVisualizationModeType::None:
        {
            // Nop
            break;
        }
    }

    return ImageRect(
        ImageCoordinates(
            region.origin.x,
            region.origin.y),
        ImageSize(
            region.size.width,
            region.size.height));
}

void ModelController::RenderStructureInto(
    ShipSpaceRect const & structureRegion,
    RgbaImageData & texture) const
{
    assert(mModel.HasLayer(LayerType::Structural));

    assert(texture.Size.width == mModel.GetStructuralLayer().Buffer.Size.width
        && texture.Size.height == mModel.GetStructuralLayer().Buffer.Size.height);

    rgbaColor const emptyColor = rgbaColor(EmptyMaterialColorKey, 0); // Fully transparent

    auto const & structuralLayerBuffer = mModel.GetStructuralLayer().Buffer;

    for (int y = structureRegion.origin.y; y < structureRegion.origin.y + structureRegion.size.height; ++y)
    {
        for (int x = structureRegion.origin.x; x < structureRegion.origin.x + structureRegion.size.width; ++x)
        {
            auto const structuralMaterial = structuralLayerBuffer[{x, y}].Material;

            texture[{x, y}] = (structuralMaterial != nullptr)
                ? structuralMaterial->RenderColor
                : emptyColor;
        }
    }
}

void ModelController::UpdateElectricalLayerVisualization(ShipSpaceRect const & region)
{
    switch (mElectricalLayerVisualizationMode)
    {
        case ElectricalLayerVisualizationModeType::PixelMode:
        {
            assert(mModel.HasLayer(LayerType::Electrical));

            assert(mElectricalLayerVisualizationTexture);
            assert(mElectricalLayerVisualizationTexture->Size.width == mModel.GetShipSize().width
                && mElectricalLayerVisualizationTexture->Size.height == mModel.GetShipSize().height);

            rgbaColor const emptyColor = rgbaColor(EmptyMaterialColorKey, 0); // Fully transparent

            auto const & electricalLayerBuffer = mModel.GetElectricalLayer().Buffer;
            RgbaImageData & electricalRenderColorTexture = *mElectricalLayerVisualizationTexture;

            for (int y = region.origin.y; y < region.origin.y + region.size.height; ++y)
            {
                for (int x = region.origin.x; x < region.origin.x + region.size.width; ++x)
                {
                    auto const electricalMaterial = electricalLayerBuffer[{x, y}].Material;

                    electricalRenderColorTexture[{x, y}] = electricalMaterial != nullptr
                        ? rgbaColor(electricalMaterial->RenderColor, 255)
                        : emptyColor;
                }
            }

            break;
        }

        case ElectricalLayerVisualizationModeType::None:
        {
            return;
        }
    }
}

void ModelController::UpdateRopesLayerVisualization()
{
    switch (mRopesLayerVisualizationMode)
    {
        case RopesLayerVisualizationModeType::LinesMode:
        {
            assert(mModel.HasLayer(LayerType::Ropes));

            // Nop
            break;
        }

        case RopesLayerVisualizationModeType::None:
        {
            return;
        }
    }
}

void ModelController::UpdateTextureLayerVisualization()
{
    switch(mTextureLayerVisualizationMode)
    {
        case TextureLayerVisualizationModeType::MatteMode:
        {
            assert(mModel.HasLayer(LayerType::Texture));

            // Nop
            break;
        }

        case TextureLayerVisualizationModeType::None:
        {
            return;
        }
    }
}

}