﻿/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2024-07-13
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#include "NpcDatabase.h"

#include "SimulationParameters.h"

#include <Core/GameException.h>
#include <Core/Utils.h>

static char HeadFKeyName[] = "head_f";
static char HeadBKeyName[] = "head_b";
static char HeadSKeyName[] = "head_s";
static char TorsoFKeyName[] = "torso_f";
static char TorsoBKeyName[] = "torso_b";
static char TorsoSKeyName[] = "torso_s";
static char ArmFKeyName[] = "arm_f";
static char ArmBKeyName[] = "arm_b";
static char ArmSKeyName[] = "arm_s";
static char LegFKeyName[] = "leg_f";
static char LegBKeyName[] = "leg_b";
static char LegSKeyName[] = "leg_s";

NpcDatabase NpcDatabase::Load(
    IAssetManager const & assetManager,
    MaterialDatabase const & materialDatabase,
    TextureAtlas<GameTextureDatabases::NpcTextureDatabase> const & npcTextureAtlas)
{
    picojson::value const root = assetManager.LoadNpcDatabase();
    if (!root.is<picojson::object>())
    {
        throw GameException("NPC database is not a JSON object");
    }

    picojson::object const & rootObject = root.get<picojson::object>();

    std::map<NpcSubKindIdType, HumanSubKind> humanSubKinds;
    std::map<NpcSubKindIdType, FurnitureSubKind> furnitureSubKinds;

    //
    // Humans
    //

    {
        auto const humansObject = Utils::GetMandatoryJsonObject(rootObject, "humans");

        auto const humansGlobalObject = Utils::GetMandatoryJsonObject(humansObject, "global");

        StructuralMaterial const & headMaterial = materialDatabase.GetStructuralMaterial(
            Utils::GetMandatoryJsonMember<std::string>(humansGlobalObject, "head_material"));

        StructuralMaterial const & feetMaterial = materialDatabase.GetStructuralMaterial(
            Utils::GetMandatoryJsonMember<std::string>(humansGlobalObject, "feet_material"));

        ParticleAttributesType globalHeadParticleAttributes = MakeParticleAttributes(humansGlobalObject, "head_particle_attributes_overrides", MakeDefaultParticleAttributes(headMaterial));

        ParticleAttributesType globalFeetParticleAttributes = MakeParticleAttributes(humansGlobalObject, "feet_particle_attributes_overrides", MakeDefaultParticleAttributes(feetMaterial));

        DefaultHumanTextureGeometryType defaultTextureGeometry = ParseDefaultHumanTextureGeometry(humansGlobalObject);

        NpcSubKindIdType nextSubKindId = 0;
        auto const humanSubKindsArray = Utils::GetMandatoryJsonArray(humansObject, "sub_kinds");
        for (auto const & humanSubKindArrayElement : humanSubKindsArray)
        {
            if (!humanSubKindArrayElement.is<picojson::object>())
            {
                throw GameException("Human NPC sub-kind array element is not a JSON object");
            }

            HumanSubKind subKind = ParseHumanSubKind(
                humanSubKindArrayElement.get<picojson::object>(),
                headMaterial,
                feetMaterial,
                globalHeadParticleAttributes,
                globalFeetParticleAttributes,
                defaultTextureGeometry,
                npcTextureAtlas);

            humanSubKinds.try_emplace(nextSubKindId, std::move(subKind));
            ++nextSubKindId;
        }
    }

    //
    // Furniture
    //

    {
        auto const furnitureObject = Utils::GetMandatoryJsonObject(rootObject, "furniture");

        NpcSubKindIdType nextSubKindId = 0;
        auto const furnitureSubKindsArray = Utils::GetMandatoryJsonArray(furnitureObject, "sub_kinds");
        for (auto const & furnitureSubKindArrayElement : furnitureSubKindsArray)
        {
            if (!furnitureSubKindArrayElement.is<picojson::object>())
            {
                throw GameException("Furniture NPC sub-kind array element is not a JSON object");
            }

            FurnitureSubKind subKind = ParseFurnitureSubKind(
                furnitureSubKindArrayElement.get<picojson::object>(),
                materialDatabase,
                npcTextureAtlas);

            furnitureSubKinds.try_emplace(nextSubKindId, std::move(subKind));
            ++nextSubKindId;
        }
    }

    //
    // Spare icons
    //

    std::vector<TextureCoordinatesQuad> spareIconTextureCoordinatesQuads;

    for (size_t f = 0; f < npcTextureAtlas.Metadata.GetFrameCount(GameTextureDatabases::NpcTextureGroups::Icon); ++f)
    {
        auto const & frameMetadata = npcTextureAtlas.Metadata.GetFrameMetadata(GameTextureDatabases::NpcTextureGroups::Icon, TextureFrameIndex(f));
        spareIconTextureCoordinatesQuads.emplace_back(TextureCoordinatesQuad{
            frameMetadata.TextureCoordinatesBottomLeft.x,
            frameMetadata.TextureCoordinatesTopRight.x,
            frameMetadata.TextureCoordinatesBottomLeft.y,
            frameMetadata.TextureCoordinatesTopRight.y});
    }

    //
    // StringTable

    StringTable stringTable = ParseStringTable(
        rootObject,
        humanSubKinds,
        furnitureSubKinds);

    //
    // Wrap it up
    //

    return NpcDatabase(
        std::move(humanSubKinds),
        std::move(furnitureSubKinds),
        std::move(spareIconTextureCoordinatesQuads),
        std::move(stringTable));
}

std::vector<std::tuple<NpcSubKindIdType, std::string>> NpcDatabase::GetHumanSubKinds(
    NpcHumanRoleType role,
    std::string const & language) const
{
    return GetSubKinds(mHumanSubKinds, mStringTable, role, language);
}

std::vector<std::tuple<NpcSubKindIdType, std::string>> NpcDatabase::GetFurnitureSubKinds(
    NpcFurnitureRoleType role,
    std::string const & language) const
{
    return GetSubKinds(mFurnitureSubKinds, mStringTable, role, language);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

NpcDatabase::NpcDatabase(
    std::map<NpcSubKindIdType, HumanSubKind> && humanSubKinds,
    std::map<NpcSubKindIdType, FurnitureSubKind> && furnitureSubKinds,
    std::vector<TextureCoordinatesQuad> && spareIconTextureCoordinatesQuads,
    StringTable && stringTable)
    : mHumanSubKinds(std::move(humanSubKinds))
    , mFurnitureSubKinds(std::move(furnitureSubKinds))
    , mSpareIconTextureCoordinatesQuads(std::move(spareIconTextureCoordinatesQuads))
    , mStringTable(std::move(stringTable))
{
    // Build lookup tables

    mHumanSubKindIdsByRole.resize(static_cast<size_t>(NpcHumanRoleType::_Last) + 1);
    for (auto const & entry : mHumanSubKinds)
    {
        mHumanSubKindIdsByRole[static_cast<uint32_t>(entry.second.Role)].push_back(entry.first);
    }

    mFurnitureSubKindIdsByRole.resize(static_cast<size_t>(NpcFurnitureRoleType::_Last) + 1);
    for (auto const & entry : mFurnitureSubKinds)
    {
        mFurnitureSubKindIdsByRole[static_cast<uint32_t>(entry.second.Role)].push_back(entry.first);
    }
}

NpcDatabase::HumanSubKind NpcDatabase::ParseHumanSubKind(
    picojson::object const & subKindObject,
    StructuralMaterial const & headMaterial,
    StructuralMaterial const & feetMaterial,
    ParticleAttributesType const & globalHeadParticleAttributes,
    ParticleAttributesType const & globalFeetParticleAttributes,
    DefaultHumanTextureGeometryType const & defaultTextureGeometry,
    TextureAtlas<GameTextureDatabases::NpcTextureDatabase> const & npcTextureAtlas)
{
    std::string const name = Utils::GetMandatoryJsonMember<std::string>(subKindObject, "name");
    NpcHumanRoleType const role = StrToNpcHumanRoleType(Utils::GetMandatoryJsonMember<std::string>(subKindObject, "role"));
    rgbColor const renderColor = Utils::Hex2RgbColor(Utils::GetMandatoryJsonMember<std::string>(subKindObject, "render_color"));

    ParticleAttributesType headParticleAttributes = MakeParticleAttributes(subKindObject, "head_particle_attributes_overrides", globalHeadParticleAttributes);
    ParticleAttributesType feetParticleAttributes = MakeParticleAttributes(subKindObject, "feet_particle_attributes_overrides", globalFeetParticleAttributes);

    float const sizeMultiplier = Utils::GetOptionalJsonMember<float>(subKindObject, "size_multiplier", 1.0f);
    float const bodyWidthRandomizationSensitivity = Utils::GetOptionalJsonMember<float>(subKindObject, "body_width_randomization_sensitivity", 1.0f);

    auto const & textureFilenameStemsObject = Utils::GetMandatoryJsonObject(subKindObject, "texture_filename_stems");
    HumanTextureFramesType textureFrames({
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, HeadFKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, HeadBKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, HeadSKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, TorsoFKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, TorsoBKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, TorsoSKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, ArmFKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, ArmBKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, ArmSKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, LegFKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, LegBKeyName, npcTextureAtlas),
        ParseTextureCoordinatesQuad(textureFilenameStemsObject, LegSKeyName, npcTextureAtlas)
        });

    auto const textureGeometry = ParseHumanTextureGeometry(
        subKindObject,
        defaultTextureGeometry,
        textureFilenameStemsObject,
        npcTextureAtlas,
        name);

    return HumanSubKind({
        std::move(name),
        role,
        renderColor,
        headMaterial,
        feetMaterial,
        {feetParticleAttributes, headParticleAttributes},
        sizeMultiplier,
        bodyWidthRandomizationSensitivity,
        textureFrames,
        textureGeometry });
}

NpcDatabase::DefaultHumanTextureGeometryType NpcDatabase::ParseDefaultHumanTextureGeometry(
    picojson::object const & containerObject)
{
    // Lengths default to human geometry
    float headLengthFraction = SimulationParameters::HumanNpcGeometry::HeadLengthFraction;
    float torsoLengthFraction = SimulationParameters::HumanNpcGeometry::TorsoLengthFraction;
    float armLengthFraction = SimulationParameters::HumanNpcGeometry::ArmLengthFraction;
    float legLengthFraction = SimulationParameters::HumanNpcGeometry::LegLengthFraction;

    // When these are not specified, will default to texture's ratios (unless subkind overrides)
    std::optional<float> headWHRatio;
    std::optional<float> torsoWHRatio;
    std::optional<float> armWHRatio;
    std::optional<float> legWHRatio;

    auto const defaultTextureGeometryContainerObject = Utils::GetOptionalJsonObject(containerObject, "default_texture_geometry");
    if (defaultTextureGeometryContainerObject.has_value())
    {
        headLengthFraction = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "head_length_fraction", SimulationParameters::HumanNpcGeometry::HeadLengthFraction);
        headWHRatio = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "head_wh_ratio");
        torsoLengthFraction = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "torso_length_fraction", SimulationParameters::HumanNpcGeometry::TorsoLengthFraction);
        torsoWHRatio = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "torso_wh_ratio");
        armLengthFraction = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "arm_length_fraction", SimulationParameters::HumanNpcGeometry::ArmLengthFraction);
        armWHRatio = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "arm_wh_ratio");
        legLengthFraction = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "leg_length_fraction", SimulationParameters::HumanNpcGeometry::LegLengthFraction);
        legWHRatio = Utils::GetOptionalJsonMember<float>(*defaultTextureGeometryContainerObject, "leg_wh_ratio");
    }

    return DefaultHumanTextureGeometryType{
        headLengthFraction,
        headWHRatio,
        torsoLengthFraction,
        torsoWHRatio,
        armLengthFraction,
        armWHRatio,
        legLengthFraction,
        legWHRatio
    };
}

NpcDatabase::HumanTextureGeometryType NpcDatabase::ParseHumanTextureGeometry(
    picojson::object const & containerObject,
    DefaultHumanTextureGeometryType const & defaults,
    picojson::object const & textureFilenameStemsContainerObject,
    TextureAtlas<GameTextureDatabases::NpcTextureDatabase> const & npcTextureAtlas,
    std::string const & subKindName)
{
    auto const textureGeometryContainerObject = Utils::GetOptionalJsonObject(containerObject, "texture_geometry_overrides");

    //
    // HeadHMultiplier: factor to multiply with Vitruvian head length for actual texture H;
    //                  expected > 1.0 for e.g. hats. Width is then given, like everything
    //                  else, by WHRatio
    //
    // Legs, Arms, Torso, Head WHRatio's: defaults from texture, but can be overridden
    //

    // Head

    float const headLengthFraction = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "head_length_fraction", defaults.HeadLengthFraction)
        : defaults.HeadLengthFraction;

    auto const headFSize = GetFrameSize(textureFilenameStemsContainerObject, HeadFKeyName, npcTextureAtlas);
    if (headFSize != GetFrameSize(textureFilenameStemsContainerObject, HeadBKeyName, npcTextureAtlas)
        || headFSize != GetFrameSize(textureFilenameStemsContainerObject, HeadSKeyName, npcTextureAtlas))
    {
        throw GameException("Head dimensions are not all equal for " + subKindName);
    }

    float const _defaultHeadWHRatio = defaults.HeadWHRatio.value_or(static_cast<float>(headFSize.width) / static_cast<float>(headFSize.height));
    float const headWHRatio = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "head_wh_ratio", _defaultHeadWHRatio)
        : _defaultHeadWHRatio;

    // Torso

    float const torsoLengthFraction = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "torso_length_fraction", defaults.TorsoLengthFraction)
        : defaults.TorsoLengthFraction;

    auto const torsoFSize = GetFrameSize(textureFilenameStemsContainerObject, TorsoFKeyName, npcTextureAtlas);
    if (torsoFSize != GetFrameSize(textureFilenameStemsContainerObject, TorsoBKeyName, npcTextureAtlas)
        || torsoFSize != GetFrameSize(textureFilenameStemsContainerObject, TorsoSKeyName, npcTextureAtlas))
    {
        throw GameException("Torso dimensions are not all equal for " + subKindName);
    }

    float const _defaultTorsoWHRatio = defaults.TorsoWHRatio.value_or(static_cast<float>(torsoFSize.width) / static_cast<float>(torsoFSize.height));
    float const torsoWHRatio = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "torso_wh_ratio", _defaultTorsoWHRatio)
        : _defaultTorsoWHRatio;

    // Arm

    float const armLengthFraction = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "arm_length_fraction", defaults.ArmLengthFraction)
        : defaults.ArmLengthFraction;

    auto const armFSize = GetFrameSize(textureFilenameStemsContainerObject, ArmFKeyName, npcTextureAtlas);
    if (armFSize != GetFrameSize(textureFilenameStemsContainerObject, ArmBKeyName, npcTextureAtlas)
        || armFSize != GetFrameSize(textureFilenameStemsContainerObject, ArmSKeyName, npcTextureAtlas))
    {
        throw GameException("Arm dimensions are not all equal for " + subKindName);
    }

    float const _defaultArmWHRatio = defaults.ArmWHRatio.value_or(static_cast<float>(armFSize.width) / static_cast<float>(armFSize.height));
    float const armWHRatio = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "arm_wh_ratio", _defaultArmWHRatio)
        : _defaultArmWHRatio;

    // Leg

    float const legLengthFraction = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "leg_length_fraction", defaults.LegLengthFraction)
        : defaults.LegLengthFraction;

    auto const legFSize = GetFrameSize(textureFilenameStemsContainerObject, LegFKeyName, npcTextureAtlas);
    if (legFSize != GetFrameSize(textureFilenameStemsContainerObject, LegBKeyName, npcTextureAtlas)
        || legFSize != GetFrameSize(textureFilenameStemsContainerObject, LegSKeyName, npcTextureAtlas))
    {
        throw GameException("Leg dimensions are not all equal for " + subKindName);
    }

    float const _defaultLegWHRatio = defaults.LegWHRatio.value_or(static_cast<float>(legFSize.width) / static_cast<float>(legFSize.height));
    float const legWHRatio = textureGeometryContainerObject.has_value()
        ? Utils::GetOptionalJsonMember<float>(*textureGeometryContainerObject, "leg_wh_ratio", _defaultLegWHRatio)
        : _defaultLegWHRatio;

    return HumanTextureGeometryType({
        headLengthFraction,
        headWHRatio,
        torsoLengthFraction,
        torsoWHRatio,
        armLengthFraction,
        armWHRatio,
        legLengthFraction,
        legWHRatio });
}

ImageSize NpcDatabase::GetFrameSize(
    picojson::object const & containerObject,
    std::string const & frameNameMemberName,
    TextureAtlas<GameTextureDatabases::NpcTextureDatabase> const & npcTextureAtlas)
{
    std::string const & frameFilenameStem = Utils::GetMandatoryJsonMember<std::string>(containerObject, frameNameMemberName);
    auto const & atlasFrameMetadata = npcTextureAtlas.Metadata.GetFrameMetadata(frameFilenameStem);
    return atlasFrameMetadata.FrameMetadata.Size;
}

NpcDatabase::FurnitureSubKind NpcDatabase::ParseFurnitureSubKind(
    picojson::object const & subKindObject,
    MaterialDatabase const & materialDatabase,
    TextureAtlas<GameTextureDatabases::NpcTextureDatabase> const & npcTextureAtlas)
{
    std::string const name = Utils::GetMandatoryJsonMember<std::string>(subKindObject, "name");
    NpcFurnitureRoleType const role = StrToNpcFurnitureRoleType(Utils::GetMandatoryJsonMember<std::string>(subKindObject, "role"));
    rgbColor const renderColor = Utils::Hex2RgbColor(Utils::GetMandatoryJsonMember<std::string>(subKindObject, "render_color"));

    StructuralMaterial const & material = materialDatabase.GetStructuralMaterial(
        Utils::GetMandatoryJsonMember<std::string>(subKindObject, "material"));

    std::string const & frameFilenameStem = Utils::GetMandatoryJsonMember<std::string>(subKindObject, "texture_filename_stem");
    auto const & atlasFrameMetadata = npcTextureAtlas.Metadata.GetFrameMetadata(frameFilenameStem);

    auto const & particleMeshObject = Utils::GetMandatoryJsonObject(subKindObject, "particle_mesh");
    ParticleMeshKindType const particleMeshKind = StrToParticleMeshKindType(
        Utils::GetMandatoryJsonMember<std::string>(particleMeshObject, "kind"));

    size_t particleCount = 0;
    FurnitureGeometryType geometry{ 0.0f, 0.0f };
    switch (particleMeshKind)
    {
        case ParticleMeshKindType::Dipole:
        {
            particleCount = 2;

            geometry = FurnitureGeometryType({
                0,
                Utils::GetMandatoryJsonMember<float>(particleMeshObject, "height") });

            break;
        }

        case ParticleMeshKindType::Particle:
        {
            particleCount = 1;

            geometry = FurnitureGeometryType({
                0,
                0 });

            break;
        }

        case ParticleMeshKindType::Quad:
        {
            particleCount = 4;

            float const height = Utils::GetMandatoryJsonMember<float>(particleMeshObject, "height");

            // Calculate width based off texture frame
            float const textureFrameAspectRatio =
                static_cast<float>(atlasFrameMetadata.FrameMetadata.Size.width)
                / static_cast<float>(atlasFrameMetadata.FrameMetadata.Size.height);
            float const width = height * textureFrameAspectRatio;

            geometry = FurnitureGeometryType({
                width,
                height });

            break;
        }
    }

    ParticleAttributesType const defaultParticleAttributes = MakeDefaultParticleAttributes(material);
    std::vector<ParticleAttributesType> particleAttributes;
    auto const jsonParticleAttributesOverrides = Utils::GetOptionalJsonArray(particleMeshObject, "particle_attributes_overrides");
    if (jsonParticleAttributesOverrides.has_value())
    {
        if (jsonParticleAttributesOverrides->size() == particleCount)
        {
            // As-is, one for each particle
            for (auto const & bvfV : *jsonParticleAttributesOverrides)
            {
                particleAttributes.push_back(
                    MakeParticleAttributes(
                        Utils::GetJsonValueAsObject(bvfV, "particle_attributes_overrides"),
                        defaultParticleAttributes));
            }
        }
        else if (jsonParticleAttributesOverrides->size() == 1)
        {
            // Repeat
            particleAttributes = std::vector<ParticleAttributesType>(
                particleCount,
                MakeParticleAttributes(
                    Utils::GetJsonValueAsObject((*jsonParticleAttributesOverrides)[0], "particle_attributes_overrides"),
                    defaultParticleAttributes));
        }
        else
        {
            throw GameException("Invalid size of particle_attributes_overrides for furniture NPC \"" + name + "\"");
        }
    }
    else
    {
        // Use material's and defaults for all particles
        particleAttributes = std::vector<ParticleAttributesType>(
            particleCount,
            defaultParticleAttributes);
    }

    TextureCoordinatesQuad textureCoordinatesQuad = TextureCoordinatesQuad({
        atlasFrameMetadata.TextureCoordinatesBottomLeft.x,
        atlasFrameMetadata.TextureCoordinatesTopRight.x,
        atlasFrameMetadata.TextureCoordinatesBottomLeft.y,
        atlasFrameMetadata.TextureCoordinatesTopRight.y });

    return FurnitureSubKind({
        std::move(name),
        role,
        renderColor,
        material,
        std::move(particleAttributes),
        particleMeshKind,
        geometry,
        std::move(textureCoordinatesQuad) });
}

NpcDatabase::ParticleAttributesType NpcDatabase::MakeParticleAttributes(
    picojson::object const & containerObject,
    std::string const & particleAttributesOverrideMemberName,
    ParticleAttributesType const & defaultParticleAttributes)
{
    auto const overridesJson = Utils::GetOptionalJsonObject(containerObject, particleAttributesOverrideMemberName);
    if (overridesJson.has_value())
    {
        return MakeParticleAttributes(*overridesJson, defaultParticleAttributes);
    }
    else
    {
        return defaultParticleAttributes;
    }
}

NpcDatabase::ParticleAttributesType NpcDatabase::MakeParticleAttributes(
    picojson::object const & particleAttributesOverrideJsonObject,
    ParticleAttributesType const & defaultParticleAttributes)
{
    float const buoyancyVolumeFill = Utils::GetOptionalJsonMember<float>(particleAttributesOverrideJsonObject, "buoyancy_volume_fill", defaultParticleAttributes.BuoyancyVolumeFill);
    float const springReductionFraction = Utils::GetOptionalJsonMember<float>(particleAttributesOverrideJsonObject, "spring_reduction_fraction", defaultParticleAttributes.SpringReductionFraction);
    float const springDampingCoefficient = Utils::GetOptionalJsonMember<float>(particleAttributesOverrideJsonObject, "spring_damping_coefficient", defaultParticleAttributes.SpringDampingCoefficient);
    float const frictionSurfaceAdjustment = Utils::GetOptionalJsonMember<float>(particleAttributesOverrideJsonObject, "friction_surface_adjustment", 1.0f);

    return ParticleAttributesType{
        buoyancyVolumeFill,
        springReductionFraction,
        springDampingCoefficient,
        frictionSurfaceAdjustment
    };
}

NpcDatabase::ParticleAttributesType NpcDatabase::MakeDefaultParticleAttributes(StructuralMaterial const & baseMaterial)
{
    float constexpr DefaultSpringReductionFraction = 0.97f;
    float constexpr DefaultSpringDampingCoefficient = 0.5f * 0.906f;
    float constexpr DefaultFrictionSurfaceAdjustment = 1.0f;

    return ParticleAttributesType{
        baseMaterial.BuoyancyVolumeFill,
        DefaultSpringReductionFraction,
        DefaultSpringDampingCoefficient,
        DefaultFrictionSurfaceAdjustment
    };
}

TextureCoordinatesQuad NpcDatabase::ParseTextureCoordinatesQuad(
    picojson::object const & containerObject,
    std::string const & memberName,
    TextureAtlas<GameTextureDatabases::NpcTextureDatabase> const & npcTextureAtlas)
{
    std::string const & frameFilenameStem = Utils::GetMandatoryJsonMember<std::string>(containerObject, memberName);
    auto const & atlasFrameMetadata = npcTextureAtlas.Metadata.GetFrameMetadata(frameFilenameStem);
    return TextureCoordinatesQuad({
        atlasFrameMetadata.TextureCoordinatesBottomLeft.x,
        atlasFrameMetadata.TextureCoordinatesTopRight.x,
        atlasFrameMetadata.TextureCoordinatesBottomLeft.y,
        atlasFrameMetadata.TextureCoordinatesTopRight.y });
}

template<typename TNpcSubKindContainer, typename TNpcRoleType>
std::vector<std::tuple<NpcSubKindIdType, std::string>> NpcDatabase::GetSubKinds(
    TNpcSubKindContainer const & container,
    StringTable const & stringTable,
    TNpcRoleType role,
    std::string const & language)
{
    std::vector<std::tuple<NpcSubKindIdType, std::string>> subKindIds;

    for (auto const & it : container)
    {
        if (it.second.Role == role)
        {
            std::string name = it.second.Name;

            // Try to localize it
            auto const itName = stringTable.find(it.second.Name);
            if (itName != stringTable.cend())
            {
                auto const itLang = std::find_if(
                    itName->second.cbegin(),
                    itName->second.cend(),
                    [&language](auto const & entry)
                    {
                        return entry.Language == language;
                    });

                if (itLang != itName->second.cend())
                {
                    name = itLang->Value;
                }
            }

            subKindIds.emplace_back(it.first, name);
        }
    }

    return subKindIds;
}

NpcDatabase::ParticleMeshKindType NpcDatabase::StrToParticleMeshKindType(std::string const & str)
{
    if (Utils::CaseInsensitiveEquals(str, "Dipole"))
        return ParticleMeshKindType::Dipole;
    else if (Utils::CaseInsensitiveEquals(str, "Particle"))
        return ParticleMeshKindType::Particle;
    else if (Utils::CaseInsensitiveEquals(str, "Quad"))
        return ParticleMeshKindType::Quad;
    else
        throw GameException("Unrecognized ParticleMeshKindType \"" + str + "\"");
}

NpcDatabase::StringTable NpcDatabase::ParseStringTable(
    picojson::object const & containerObject,
    std::map<NpcSubKindIdType, HumanSubKind> const & humanSubKinds,
    std::map<NpcSubKindIdType, FurnitureSubKind> const & furnitureSubKinds)
{
    StringTable stringTable;

    //
    // 1 - Prepare keys (en)
    //

    for (auto const & entry : humanSubKinds)
    {
        // Ignore dupes
        stringTable.try_emplace(entry.second.Name, std::vector<StringEntry>({ StringEntry("en", entry.second.Name) }));
    }

    for (auto const & entry : furnitureSubKinds)
    {
        // Ignore dupes
        stringTable.try_emplace(entry.second.Name, std::vector<StringEntry>({ StringEntry("en", entry.second.Name) }));
    }

    //
    // 2 - Parse
    //

    picojson::object const & stringTableJsonObject = Utils::GetMandatoryJsonObject(containerObject, "string_table");
    for (auto const & langEntry : stringTableJsonObject)
    {
        std::string const & language = langEntry.first;

        picojson::object const & nameMappingsJsonObject = Utils::GetJsonValueAsObject(langEntry.second, language);
        for (auto const & nameMapping : nameMappingsJsonObject)
        {
            // Name must be in keys
            auto itNameKey = stringTable.find(nameMapping.first);
            if (itNameKey == stringTable.cend())
            {
                throw GameException("Name key \"" + nameMapping.first + "\" in string table for language \"" + language + "\" is not known");
            }

            // Lang must not be there
            auto const itLang = std::find_if(
                itNameKey->second.cbegin(),
                itNameKey->second.cend(),
                [&language](StringEntry const & entry)
                {
                    return entry.Language == language;
                });
            if (itLang != itNameKey->second.cend())
            {
                throw GameException("Language \"" + language + "\" appears more than once in string table for name \"" + nameMapping.first + "\"");
            }

            // Store
            itNameKey->second.push_back({ language, Utils::GetJsonValueAs<std::string>(nameMapping.second, nameMapping.first) });
        }
    }

    return stringTable;
}
