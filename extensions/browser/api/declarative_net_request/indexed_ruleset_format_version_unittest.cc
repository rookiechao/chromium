// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "extensions/browser/api/declarative_net_request/utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {
namespace declarative_net_request {
namespace {

const char* kFlatbufferSchemaExpected = R"(
include "components/url_pattern_index/flat/url_pattern_index.fbs";
namespace extensions.declarative_net_request.flat;
table UrlRuleMetadata {
  id : uint (key);
  redirect_url : string;
}
enum ActionIndex : ubyte {
  block = 0,
  allow,
  redirect,
  count
}
table ExtensionIndexedRuleset {
  index_list : [url_pattern_index.flat.UrlPatternIndex];
  extension_metadata : [UrlRuleMetadata];
}
root_type ExtensionIndexedRuleset;
file_identifier "EXTR";
)";

const char* kSingleLineComment = "//";
const char* kMultiLineCommentStart = "/*";
const char* kMultiLineCommentEnd = "*/";
const char* kNewline = "\n";

// Strips comments from |input| and removes all whitespace. Note: this is not a
// rigorous implementation.
std::string StripCommentsAndWhitespace(const std::string& input) {
  std::string result;

  for (auto& line : base::SplitString(input, kNewline, base::KEEP_WHITESPACE,
                                      base::SPLIT_WANT_NONEMPTY)) {
    // Remove single line comments.
    size_t index = line.find(kSingleLineComment);
    if (index != std::string::npos)
      line.erase(index);

    // Remove any whitespace.
    std::string str;
    base::RemoveChars(line, base::kWhitespaceASCII, &str);
    result += str;
  }

  // Remove multi line comments.
  while (true) {
    size_t start = result.find(kMultiLineCommentStart);
    if (start == std::string::npos)
      break;

    size_t end = result.find(kMultiLineCommentEnd, start + 2);
    // No ending found for the comment.
    if (end == std::string::npos)
      break;

    size_t end_comment_index = end + 1;
    size_t comment_length = end_comment_index - start + 1;
    result.erase(start, comment_length);
  }

  return result;
}

using IndexedRulesetFormatVersionTest = ::testing::Test;

// Ensures that we update the indexed ruleset format version when the flatbuffer
// schema is modified.
TEST_F(IndexedRulesetFormatVersionTest, CheckVersionUpdated) {
  base::FilePath source_root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SOURCE_ROOT, &source_root));

  base::FilePath flatbuffer_schema_path = source_root.AppendASCII(
      "extensions/browser/api/declarative_net_request/flat/"
      "extension_ruleset.fbs");
  ASSERT_TRUE(base::PathExists(flatbuffer_schema_path));

  std::string flatbuffer_schema;
  ASSERT_TRUE(
      base::ReadFileToString(flatbuffer_schema_path, &flatbuffer_schema));

  EXPECT_EQ(StripCommentsAndWhitespace(kFlatbufferSchemaExpected),
            StripCommentsAndWhitespace(flatbuffer_schema))
      << "Schema change detected; update this test and the schema version.";
  EXPECT_EQ(6, GetIndexedRulesetFormatVersionForTesting())
      << "Update this test if you update the schema version.";
}

// Test to sanity check the behavior of StripCommentsAndWhitespace.
TEST_F(IndexedRulesetFormatVersionTest, StripCommentsAndWhitespace) {
  std::string input = R"(
      // This is a single line comment.
      Some text // Another comment.
      /* Multi-line
        Comment */ More text.
      /* Another multi-line
      comment */ Yet more text.
  )";

  std::string expected_output = "SometextMoretext.Yetmoretext.";
  EXPECT_EQ(expected_output, StripCommentsAndWhitespace(input));
}

}  // namespace
}  // namespace declarative_net_request
}  // namespace extensions
