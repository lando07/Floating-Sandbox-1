/***************************************************************************************
* Original Author:      Gabriele Giuseppini
* Created:              2018-12-07
* Copyright:            Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#include "MaterialDatabase.h"

MaterialDatabase MaterialDatabase::Load(std::filesystem::path materialsRootDirectory)
{
    //
    // Structural
    //

    std::map<ColorKey, StructuralMaterial> structuralMaterialsMap;
    UniqueStructuralMaterialsArray uniqueStructuralMaterials;

    for (size_t i = 0; i < uniqueStructuralMaterials.size(); ++i)
        uniqueStructuralMaterials[i].second = nullptr;

    picojson::value const structuralMaterialsRoot = Utils::ParseJSONFile(
        materialsRootDirectory / "materials_structural.json");

    if (!structuralMaterialsRoot.is<picojson::object>())
    {
        throw GameException("Structural materials definition is not a JSON object");
    }

    float largestMass = 0.0f;
    float largestStrength = 0.0f;

    // Parse palette structure
    std::vector<PaletteCategory> structuralPaletteCategories = ParsePaletteCategories(
        Utils::GetMandatoryJsonArray(structuralMaterialsRoot.get<picojson::object>(), "palette_categories"));

    // Parse materials
    picojson::array const & structuralMaterialsRootArray = Utils::GetMandatoryJsonArray(structuralMaterialsRoot.get<picojson::object>(), "materials");
    for (auto const & materialElem : structuralMaterialsRootArray)
    {
        if (!materialElem.is<picojson::object>())
        {
            throw GameException("Found a non-object in structural materials definition");
        }

        picojson::object const & materialObject = materialElem.get<picojson::object>();

        // Normalize color keys
        std::vector<ColorKey> colorKeys;
        {
            auto const & memberIt = materialObject.find("color_key");
            if (materialObject.end() == memberIt)
            {
                throw GameException("Error parsing JSON: cannot find member \"color_key\"");
            }

            if (memberIt->second.is<std::string>())
            {
                colorKeys.emplace_back(
                    Utils::Hex2RgbColor(
                        memberIt->second.get<std::string>()));
            }
            else if (memberIt->second.is<picojson::array>())
            {
                auto const & memberArray = memberIt->second.get<picojson::array>();
                for (auto const & memberArrayElem : memberArray)
                {
                    if (!memberArrayElem.is<std::string>())
                    {
                        throw GameException("Error parsing JSON: an element of the material's \"color_key\" array is not a 'string'");
                    }

                    colorKeys.emplace_back(
                        Utils::Hex2RgbColor(
                            memberArrayElem.get<std::string>()));
                }
            }
            else
            {
                throw GameException("Error parsing JSON: material's \"color_key\" member is neither a 'string' nor an 'array'");
            }
        }

        // Process all color keys
        for (size_t iColorKey = 0; iColorKey < colorKeys.size(); ++iColorKey)
        {
            rgbColor const colorKey = colorKeys[iColorKey];

            // Get/make render color
            rgbColor renderColor = colorKey;
            auto const & renderColorIt = materialObject.find("render_color");
            if (materialObject.end() != renderColorIt)
            {
                // The material carries its own render color

                if (colorKeys.size() > 1)
                {
                    throw GameException("Error parsing JSON: material with multiple \"color_key\" members cannot specify custom \"render_color\" members");
                }

                if (!renderColorIt->second.is<std::string>())
                {
                    throw GameException("Error parsing JSON: member \"render_color\" is not of type 'string'");
                }

                renderColor = Utils::Hex2RgbColor(renderColorIt->second.get<std::string>());
            }

            // Create instance of this material
            StructuralMaterial const material = StructuralMaterial::Create(
                static_cast<unsigned int>(iColorKey),
                renderColor,
                materialObject);

            // Validate palette coordinates
            if (material.PaletteCoordinates.has_value())
            {
                // TODOHERE
            }

            // Make sure there are no dupes
            if (structuralMaterialsMap.count(colorKey) != 0)
            {
                throw GameException("Structural material \"" + material.Name + "\" has a duplicate color key");
            }

            // Store
            auto const storedEntry = structuralMaterialsMap.emplace(
                std::make_pair(
                    colorKey,
                    material));

            // Check if it's a unique material, and if so, check for dupes and store it
            if (material.UniqueType.has_value())
            {
                size_t uniqueTypeIndex = static_cast<size_t>(*(material.UniqueType));
                if (nullptr != uniqueStructuralMaterials[uniqueTypeIndex].second)
                {
                    throw GameException("More than one unique material of type \"" + std::to_string(uniqueTypeIndex) + "\" found in structural materials definition");
                }

                uniqueStructuralMaterials[uniqueTypeIndex] = std::make_pair(
                    colorKey,
                    &(storedEntry.first->second));
            }

            // Update extremes
            largestMass = std::max(material.GetMass(), largestMass);
            largestStrength = std::max(material.Strength, largestStrength);
        }
    }

    // Make sure we did find all the unique materials
    for (size_t i = 0; i < uniqueStructuralMaterials.size(); ++i)
    {
        if (nullptr == uniqueStructuralMaterials[i].second)
        {
            throw GameException("No material found in structural materials definition for unique type \"" + std::to_string(i) + "\"");
        }
    }

    // Make sure there are no clashes with indexed rope colors
    for (auto const & entry : structuralMaterialsMap)
    {
        if ((!entry.second.UniqueType || StructuralMaterial::MaterialUniqueType::Rope != *(entry.second.UniqueType))
            && entry.first.r == uniqueStructuralMaterials[RopeUniqueMaterialIndex].first.r
            && (entry.first.g & 0xF0) == (uniqueStructuralMaterials[RopeUniqueMaterialIndex].first.g & 0xF0))
        {
            throw GameException("Structural material \"" + entry.second.Name + "\" has a color key that is reserved for ropes and rope endpoints");
        }
    }

    //
    // Electrical materials
    //

    std::map<ColorKey, ElectricalMaterial, NonInstancedColorKeyComparer> nonInstancedElectricalMaterialsMap;
    std::map<ColorKey, ElectricalMaterial, InstancedColorKeyComparer> instancedElectricalMaterialsMap;

    picojson::value const electricalMaterialsRoot = Utils::ParseJSONFile(
        materialsRootDirectory / "materials_electrical.json");

    if (!electricalMaterialsRoot.is<picojson::object>())
    {
        throw GameException("Structural materials definition is not a JSON object");
    }

    // Parse palette structure
    std::vector<PaletteCategory> electricalPaletteCategories = ParsePaletteCategories(
        Utils::GetMandatoryJsonArray(electricalMaterialsRoot.get<picojson::object>(), "palette_categories"));

    // Parse materials
    picojson::array const & electricalMaterialsRootArray = Utils::GetMandatoryJsonArray(electricalMaterialsRoot.get<picojson::object>(), "materials");
    for (auto const & materialElem : electricalMaterialsRootArray)
    {
        if (!materialElem.is<picojson::object>())
        {
            throw GameException("Found a non-object in electrical materials definition");
        }

        picojson::object const & materialObject = materialElem.get<picojson::object>();

        ColorKey const colorKey = Utils::Hex2RgbColor(
            Utils::GetMandatoryJsonMember<std::string>(materialObject, "color_key"));

        ElectricalMaterial const material = ElectricalMaterial::Create(materialObject);

        // Validate palette coordinates
        if (material.PaletteCoordinates.has_value())
        {
            // TODOHERE
        }

        // Make sure there are no dupes
        if (nonInstancedElectricalMaterialsMap.count(colorKey) != 0
            || instancedElectricalMaterialsMap.count(colorKey) != 0)
        {
            throw GameException("Electrical material \"" + material.Name + "\" has a duplicate color key");
        }

        // Store
        if (material.IsInstanced)
        {
            instancedElectricalMaterialsMap.emplace(
                std::make_pair(
                    colorKey,
                    material));
        }
        else
        {
            nonInstancedElectricalMaterialsMap.emplace(
                std::make_pair(
                    colorKey,
                    material));
        }
    }

    //
    // Make sure there are no structural materials whose key appears
    // in electrical materials, with the exception for "legacy" electrical
    // materials
    //

    for (auto const & kv : structuralMaterialsMap)
    {
        if (!kv.second.IsLegacyElectrical
            && (0 != nonInstancedElectricalMaterialsMap.count(kv.first)
                || 0 != instancedElectricalMaterialsMap.count(kv.first)))
        {
            throw GameException("color key of structural material \"" + kv.second.Name + "\" is also present among electrical materials");
        }
    }

    return MaterialDatabase(
        std::move(structuralMaterialsMap),
        std::move(structuralPaletteCategories),
        std::move(nonInstancedElectricalMaterialsMap),
        std::move(instancedElectricalMaterialsMap),
        uniqueStructuralMaterials,
        largestMass,
        largestStrength);
}