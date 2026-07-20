#include <gtest/gtest.h>

#include "robot_teacher/dialog_utils.hpp"

using robot_teacher::SessionState;
using robot_teacher::stateName;
using robot_teacher::jsonEscape;
using robot_teacher::parseContent;

// ── stateName ────────────────────────────────────────────────────
TEST(DialogUtilsStateName, CoversAllEnumerators) {
    EXPECT_STREQ(stateName(SessionState::IDLE),      "IDLE");
    EXPECT_STREQ(stateName(SessionState::GREETING),  "GREETING");
    EXPECT_STREQ(stateName(SessionState::DIALOG),    "DIALOG");
    EXPECT_STREQ(stateName(SessionState::LISTENING), "LISTENING");
    EXPECT_STREQ(stateName(SessionState::PAUSED),    "PAUSED");
    EXPECT_STREQ(stateName(SessionState::BYE),       "BYE");
}

// ── jsonEscape ───────────────────────────────────────────────────
TEST(DialogUtilsJsonEscape, PlainTextIsUnchanged) {
    EXPECT_EQ(jsonEscape("hello world"), "hello world");
    EXPECT_EQ(jsonEscape(""), "");
}

TEST(DialogUtilsJsonEscape, EscapesQuotesAndBackslashes) {
    EXPECT_EQ(jsonEscape(R"(say "hi")"), R"(say \"hi\")");
    EXPECT_EQ(jsonEscape(R"(C:\path)"), R"(C:\\path)");
}

TEST(DialogUtilsJsonEscape, EscapesWhitespaceControlChars) {
    EXPECT_EQ(jsonEscape("line1\nline2"), "line1\\nline2");
    EXPECT_EQ(jsonEscape("a\tb"), "a\\tb");
    EXPECT_EQ(jsonEscape("a\rb"), "a\\rb");
}

TEST(DialogUtilsJsonEscape, HandlesCyrillicPassthrough) {
    // Non-ASCII bytes aren't part of the escape set — must pass through untouched.
    EXPECT_EQ(jsonEscape("Привет!"), "Привет!");
}

// ── parseContent ─────────────────────────────────────────────────
TEST(DialogUtilsParseContent, ExtractsAnthropicTextField) {
    const std::string body =
        R"({"id":"msg_1","content":[{"type":"text","text":"Hello there"}]})";
    EXPECT_EQ(parseContent(body, /*is_anthropic=*/true), "Hello there");
}

TEST(DialogUtilsParseContent, ExtractsOpenAiContentField) {
    const std::string body =
        R"({"choices":[{"message":{"role":"assistant","content":"Hi there"}}]})";
    EXPECT_EQ(parseContent(body, /*is_anthropic=*/false), "Hi there");
}

TEST(DialogUtilsParseContent, DecodesEscapedCharactersInReply) {
    const std::string body = R"({"text":"line1\nline2 \"quoted\""})";
    EXPECT_EQ(parseContent(body, true), "line1\nline2 \"quoted\"");
}

TEST(DialogUtilsParseContent, ReturnsEmptyWhenKeyMissing) {
    EXPECT_EQ(parseContent(R"({"error":"rate_limited"})", true), "");
    EXPECT_EQ(parseContent("", false), "");
}

TEST(DialogUtilsParseContent, WrongVariantKeyIsNotMatched) {
    // Body has OpenAI-style "content" key but we ask for Anthropic's "text" key.
    const std::string body = R"({"content":"should not be found"})";
    EXPECT_EQ(parseContent(body, /*is_anthropic=*/true), "");
}

// main() is supplied by GTest::gtest_main, linked automatically by
// ament_add_gtest() — do not define one here (would be a duplicate symbol).
