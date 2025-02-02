#include <Game/RenderTypes.h>
#include <Game/ShaderTypes.h>

#include <GameOpenGL/ShaderManager.h>

#include <GameCore/GameException.h>

#include "gtest/gtest.h"

using TestShaderManager = ShaderManager<Render::ShaderManagerTraits>;

class ShaderManagerTests : public testing::Test
{
protected:

    std::map<std::string, std::string> MakeStaticParameters()
    {
        return std::map<std::string, std::string> {
            {"Z_DEPTH", "2.5678"},
            {"FOO", "BAR"}
        };
    }
};

TEST_F(ShaderManagerTests, ProcessesIncludes_OneLevel)
{
    std::string source = R"(
aaa
  #include "inc1.glsl"
bbb
)";

    std::unordered_map<std::string, std::pair<bool, std::string>> includeFiles;
    includeFiles["ggg.glsl"] = std::make_pair<bool, std::string>(true, std::string("   \n zorro \n"));
    includeFiles["inc1.glsl"] = { false, std::string(" \n sancho \n") };

    auto resolvedSource = TestShaderManager::ResolveIncludes(
        source,
        includeFiles);

    EXPECT_EQ("\naaa\n \n sancho \n\nbbb\n", resolvedSource);
}

TEST_F(ShaderManagerTests, ProcessesIncludes_MultipleLevels)
{
    std::string source = R"(
aaa
  #include "inc1.glsl"
bbb
)";

    std::unordered_map<std::string, std::pair<bool, std::string>> includeFiles;
    includeFiles["inc2.glslinc"] = std::make_pair<bool, std::string>(true, std::string("nano\n"));
    includeFiles["inc1.glsl"] = { false, std::string("sancho\n#include \"inc2.glslinc\"") };

    auto resolvedSource = TestShaderManager::ResolveIncludes(
        source,
        includeFiles);

    EXPECT_EQ("\naaa\nsancho\nnano\n\nbbb\n", resolvedSource);
}

TEST_F(ShaderManagerTests, ProcessesIncludes_DetectsLoops)
{
    std::string source = R"(
aaa
#include "inc1.glsl"
bbb
)";

    std::unordered_map<std::string, std::pair<bool, std::string>> includeFiles;
    includeFiles["inc2.glslinc"] = std::make_pair<bool, std::string>(true, std::string("#include \"inc1.glsl\"\n"));
    includeFiles["inc1.glsl"] = { false, std::string("sancho\n#include \"inc2.glslinc\"") };

    EXPECT_THROW(
        TestShaderManager::ResolveIncludes(
            source,
            includeFiles),
        GameException);
}

TEST_F(ShaderManagerTests, ProcessesIncludes_ComplainsWhenIncludeNotFound)
{
    std::string source = R"(
aaa
  #include "inc1.glslinc"
bbb
)";

    std::unordered_map<std::string, std::pair<bool, std::string>> includeFiles;
    includeFiles["inc3.glslinc"] = std::make_pair<bool, std::string>(true, std::string("nano\n"));

    EXPECT_THROW(
        TestShaderManager::ResolveIncludes(
            source,
            includeFiles),
        GameException);
}

TEST_F(ShaderManagerTests, SplitsShaders)
{
    std::string source = R"(###VERTEX-120
vfoo
    ###FRAGMENT-999
 fbar
)";

    auto [vertexSource, fragmentSource] = TestShaderManager::SplitSource(source);

    EXPECT_EQ("#version 120\nvfoo\n", vertexSource);
    EXPECT_EQ("#version 999\n fbar\n", fragmentSource);
}

TEST_F(ShaderManagerTests, SplitsShaders_DuplicatesCommonSectionToVertexAndFragment)
{
    std::string source = R"(  #define foo bar this is common

another define
    ###VERTEX-120
vfoo
    ###FRAGMENT-120
 fbar
)";

    auto [vertexSource, fragmentSource] = TestShaderManager::SplitSource(source);

    EXPECT_EQ("#version 120\n  #define foo bar this is common\n\nanother define\nvfoo\n", vertexSource);
    EXPECT_EQ("#version 120\n  #define foo bar this is common\n\nanother define\n fbar\n", fragmentSource);
}

TEST_F(ShaderManagerTests, SplitsShaders_ErrorsOnMalformedVertexSection)
{
    std::string source = R"(###VERTEX-1a0
vfoo
    ###FRAGMENT-999
 fbar
)";

    EXPECT_THROW(
        TestShaderManager::SplitSource(source),
        GameException);
}

TEST_F(ShaderManagerTests, SplitsShaders_ErrorsOnMissingVertexSection)
{
    std::string source = R"(vfoo
###FRAGMENT
fbar
    )";

    EXPECT_THROW(
        TestShaderManager::SplitSource(source),
        GameException);
}

TEST_F(ShaderManagerTests, SplitsShaders_ErrorsOnMissingVertexSection_EmptyFile)
{
    std::string source = "";

    EXPECT_THROW(
        TestShaderManager::SplitSource(source),
        GameException);
}

TEST_F(ShaderManagerTests, SplitsShaders_ErrorsOnMissingFragmentSection)
{
    std::string source = R"(###VERTEX
vfoo
fbar
    )";

    EXPECT_THROW(
        TestShaderManager::SplitSource(source),
        GameException);
}

TEST_F(ShaderManagerTests, ParsesStaticParameters_Single)
{
    std::string source = R"(

   FOO=56.8)";

    std::map<std::string, std::string> params;
    TestShaderManager::ParseLocalStaticParameters(source, params);

    EXPECT_EQ(1u, params.size());

    auto const & it = params.find("FOO");
    ASSERT_NE(it, params.end());
    EXPECT_EQ("56.8", it->second);
}

TEST_F(ShaderManagerTests, ParsesStaticParameters_Multiple)
{
    std::string source = R"(

FOO = 67, 87, 88

BAR = 89)";

    std::map<std::string, std::string> params;
    TestShaderManager::ParseLocalStaticParameters(source, params);

    EXPECT_EQ(2u, params.size());

    auto const & it1 = params.find("FOO");
    ASSERT_NE(it1, params.end());
    EXPECT_EQ("67, 87, 88", it1->second);

    auto const & it2 = params.find("BAR");
    ASSERT_NE(it2, params.end());
    EXPECT_EQ("89", it2->second);
}

TEST_F(ShaderManagerTests, ParsesStaticParameters_ErrorsOnRepeatedParameter)
{
    std::string source = R"(

FOO = 67
BAR = 89
FOO = 67
)";

    std::map<std::string, std::string> params;
    EXPECT_THROW(
        TestShaderManager::ParseLocalStaticParameters(source, params),
        GameException);
}

TEST_F(ShaderManagerTests, ParsesStaticParameters_ErrorsOnMalformedDefinition)
{
    std::string source = R"(

FOO = 67
  g

)";

    std::map<std::string, std::string> params;
    EXPECT_THROW(
        TestShaderManager::ParseLocalStaticParameters(source, params),
        GameException);
}

TEST_F(ShaderManagerTests, SubstitutesStaticParameters_Single)
{
    std::string source = "hello world %Z_DEPTH% bar";

    auto result = TestShaderManager::SubstituteStaticParameters(source, MakeStaticParameters());

    EXPECT_EQ("hello world 2.5678 bar", result);
}

TEST_F(ShaderManagerTests, SubstitutesStaticParameters_Multiple_Different)
{
    std::string source = "hello world %Z_DEPTH% bar\n foo %FOO% hello";

    auto result = TestShaderManager::SubstituteStaticParameters(source, MakeStaticParameters());

    EXPECT_EQ("hello world 2.5678 bar\n foo BAR hello", result);
}

TEST_F(ShaderManagerTests, SubstitutesStaticParameters_Multiple_Repeated)
{
    std::string source = "hello world %Z_DEPTH% bar\n foo %Z_DEPTH% hello";

    auto result = TestShaderManager::SubstituteStaticParameters(source, MakeStaticParameters());

    EXPECT_EQ("hello world 2.5678 bar\n foo 2.5678 hello", result);
}

TEST_F(ShaderManagerTests, SubstitutesStaticParameters_ErrorsOnUnrecognizedParameter)
{
    std::string source = "a %Z_BAR% b";

    EXPECT_THROW(
        TestShaderManager::SubstituteStaticParameters(source, MakeStaticParameters()),
        GameException);
}