/**
 * UVRPC Error-String Unit Tests
 *
 * uvrpc_strerror is a switch over the error enum. The failure mode that matters
 * is not the strings themselves but the fall-through: add an error code, forget
 * the switch case, and every caller that logs the code's meaning silently gets
 * "Unknown error". That already happened once -- UVRPC_ERROR_TRANSPORT_BUSY is
 * returned from three places in uvrpc_client.c and had no case at all.
 *
 * So the central test enumerates every real code and demands a real message.
 * A new code that arrives without a case fails here rather than in a log line
 * somebody reads six months later.
 *
 * UVRPC_ERROR_MAX is the enum's terminal sentinel, not an error, so it is
 * excluded along with any code past it.
 */

#include <gtest/gtest.h>
#include <set>
#include <string>

extern "C" {
#include "../include/uvrpc.h"
}

/* Every code a caller can actually receive, in enum order. Kept explicit
 * rather than derived so that adding an enum value does not silently widen
 * the test: whoever adds the code has to decide here too. */
static const int kAllErrorCodes[] = {
    UVRPC_OK,
    UVRPC_ERROR,
    UVRPC_ERROR_INVALID_PARAM,
    UVRPC_ERROR_NO_MEMORY,
    UVRPC_ERROR_NOT_CONNECTED,
    UVRPC_ERROR_TIMEOUT,
    UVRPC_ERROR_TRANSPORT_BUSY,
    UVRPC_ERROR_TRANSPORT,
    UVRPC_ERROR_CALLBACK_LIMIT,
    UVRPC_ERROR_CANCELLED,
    UVRPC_ERROR_POOL_EXHAUSTED,
    UVRPC_ERROR_RATE_LIMITED,
    UVRPC_ERROR_NOT_FOUND,
    UVRPC_ERROR_ALREADY_EXISTS,
    UVRPC_ERROR_INVALID_STATE,
    UVRPC_ERROR_IO,
    UVRPC_ERROR_MAX_CLIENTS,
};

TEST(StrErrorTest, EveryCodeHasAMessage) {
    for (int code : kAllErrorCodes) {
        const char* msg = uvrpc_strerror(code);
        ASSERT_NE(msg, nullptr) << "code " << code;
        EXPECT_STRNE(msg, "Unknown error")
            << "code " << code << " has no case in the switch and falls through "
                            "to the default";
    }
}

TEST(StrErrorTest, MessagesAreDistinct) {
    std::set<std::string> seen;
    for (int code : kAllErrorCodes) {
        std::string msg = uvrpc_strerror(code);
        EXPECT_TRUE(seen.insert(msg).second)
            << "code " << code << " reuses the message: " << msg;
    }
}

TEST(StrErrorTest, UnknownCodeIsReportedNotCrashed) {
    EXPECT_STREQ(uvrpc_strerror(-99999), "Unknown error");
    EXPECT_STREQ(uvrpc_strerror(12345), "Unknown error");
}

/* A message has to be words, not a restatement of the number: "-1", "E" or
 * "?" would satisfy every other test here while telling a caller nothing.
 * Asserted as "contains a letter" rather than as a length, because a length
 * threshold is arbitrary -- "Success" is short and perfectly useful. */
TEST(StrErrorTest, MessagesAreWordsNotCodes) {
    for (int code : kAllErrorCodes) {
        const char* msg = uvrpc_strerror(code);
        ASSERT_NE(msg, nullptr) << "code " << code;
        bool has_letter = false;
        for (const char* c = msg; *c; ++c) {
            if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) {
                has_letter = true;
                break;
            }
        }
        EXPECT_TRUE(has_letter)
            << "code " << code << " message is not words: \"" << msg << "\"";
    }
}