#include <MediaRemoteCore.h>
#include <unity.h>

#include <stdint.h>
#include <string.h>

using namespace media_remote;

void testProgressBarWidth()
{
  TEST_ASSERT_EQUAL_INT(0, progressBarWidth(0, 1000, 300));
  TEST_ASSERT_EQUAL_INT(150, progressBarWidth(500, 1000, 300));
  TEST_ASSERT_EQUAL_INT(300, progressBarWidth(1000, 1000, 300));
  TEST_ASSERT_EQUAL_INT(0, progressBarWidth(-1, 1000, 300));
  TEST_ASSERT_EQUAL_INT(300, progressBarWidth(1500, 1000, 300));
  TEST_ASSERT_EQUAL_INT(0, progressBarWidth(500, 0, 300));
  TEST_ASSERT_EQUAL_INT(222, progressBarWidth(81LL * 60 * 1000, 162LL * 60 * 1000, 444));
  TEST_ASSERT_EQUAL_INT(150, progressBarWidth(120LL * 60 * 1000, 240LL * 60 * 1000, 300));
  TEST_ASSERT_EQUAL_INT(222, progressBarWidth(6LL * 60 * 60 * 1000, 12LL * 60 * 60 * 1000, 444));
}

void testDeadlinesAndElapsedTime()
{
  TEST_ASSERT_TRUE(deadlineReached(100, 0));
  TEST_ASSERT_FALSE(deadlineReached(99, 100));
  TEST_ASSERT_TRUE(deadlineReached(100, 100));
  TEST_ASSERT_TRUE(deadlineReached(101, 100));
  TEST_ASSERT_FALSE(deadlineReached(UINT32_MAX - 5, 4));
  TEST_ASSERT_TRUE(deadlineReached(4, 4));
  TEST_ASSERT_TRUE(deadlineReached(5, 4));
  TEST_ASSERT_EQUAL_UINT32(10, elapsedMs(4, UINT32_MAX - 5));
}

void testHttpOrigins()
{
  Origin http = parseHttpOrigin("http://ha.local/api");
  TEST_ASSERT_TRUE(http.valid);
  TEST_ASSERT_EQUAL(UrlScheme::Http, http.scheme);
  TEST_ASSERT_EQUAL_UINT16(80, http.port);
  TEST_ASSERT_EQUAL_STRING("ha.local", http.host);

  Origin https = parseHttpOrigin("HTTPS://HA.Local:443/api?x=1");
  TEST_ASSERT_TRUE(https.valid);
  TEST_ASSERT_EQUAL(UrlScheme::Https, https.scheme);
  TEST_ASSERT_EQUAL_UINT16(443, https.port);
  TEST_ASSERT_EQUAL_STRING("ha.local", https.host);

  Origin customPort = parseHttpOrigin("https://ha.local:8123/");
  TEST_ASSERT_TRUE(customPort.valid);
  TEST_ASSERT_EQUAL_UINT16(8123, customPort.port);

  Origin ipv6 = parseHttpOrigin("http://[2001:DB8::1]:8123/api");
  TEST_ASSERT_TRUE(ipv6.valid);
  TEST_ASSERT_EQUAL_STRING("2001:db8::1", ipv6.host);
  TEST_ASSERT_EQUAL_UINT16(8123, ipv6.port);
}

void testSameOriginPolicy()
{
  TEST_ASSERT_TRUE(sameOrigin("https://HA.local/api", "https://ha.LOCAL:443/other"));
  TEST_ASSERT_TRUE(sameOrigin("http://ha.local:80", "http://ha.local/path"));
  TEST_ASSERT_FALSE(sameOrigin("http://ha.local", "https://ha.local"));
  TEST_ASSERT_FALSE(sameOrigin("https://ha.local", "https://ha.local:8123"));
  TEST_ASSERT_FALSE(sameOrigin("https://ha.local", "https://other.local"));
  TEST_ASSERT_FALSE(sameOrigin("https://user@ha.local", "https://ha.local"));
  TEST_ASSERT_FALSE(sameOrigin("https://ha.local:bad", "https://ha.local"));
  TEST_ASSERT_FALSE(sameOrigin("ftp://ha.local", "https://ha.local"));
  TEST_ASSERT_FALSE(sameOrigin("https://", "https://ha.local"));
  TEST_ASSERT_FALSE(sameOrigin(nullptr, "https://ha.local"));
}

void testSha256FingerprintNormalization()
{
  char output[65];
  TEST_ASSERT_TRUE(normalizeSha256Fingerprint(
    "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING(
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", output);
  TEST_ASSERT_TRUE(normalizeSha256Fingerprint(
    "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF",
    output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING(
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", output);
  TEST_ASSERT_FALSE(normalizeSha256Fingerprint("0011", output, sizeof(output)));
  TEST_ASSERT_FALSE(normalizeSha256Fingerprint("", output, sizeof(output)));
  TEST_ASSERT_FALSE(normalizeSha256Fingerprint(
    "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFG", output, sizeof(output)));
  TEST_ASSERT_FALSE(normalizeSha256Fingerprint(nullptr, output, sizeof(output)));
}

void testRedirectPolicy()
{
  char output[256];
  TEST_ASSERT_TRUE(resolveRedirectUrl("https://ha.local/api/image", "/cover.jpg", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("https://ha.local/cover.jpg", output);
  TEST_ASSERT_TRUE(resolveRedirectUrl("https://ha.local/api/image", "next.jpg", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("https://ha.local/api/next.jpg", output);
  TEST_ASSERT_TRUE(resolveRedirectUrl("https://ha.local/api/image", "//cdn.local/art.jpg", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("https://cdn.local/art.jpg", output);
  TEST_ASSERT_TRUE(resolveRedirectUrl(
    "https://ha.local:8123/api/image", "https://cdn.local/art.jpg", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("https://cdn.local/art.jpg", output);
  TEST_ASSERT_TRUE(resolveRedirectUrl("http://[2001:db8::1]:8123/api/image", "/art", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("http://[2001:db8::1]:8123/art", output);
  TEST_ASSERT_FALSE(resolveRedirectUrl("https://ha.local/api/image", "ftp://cdn.local/art.jpg", output, sizeof(output)));

  TEST_ASSERT_TRUE(redirectAllowed("https://ha.local/a", "https://cdn.local/b"));
  TEST_ASSERT_TRUE(redirectAllowed("http://ha.local/a", "https://ha.local/b"));
  TEST_ASSERT_FALSE(redirectAllowed("https://ha.local/a", "http://ha.local/b"));
  TEST_ASSERT_FALSE(redirectAllowed("https://ha.local/a", "not-a-url"));
}

void testCredentialPolicy()
{
  TEST_ASSERT_EQUAL(CredentialAction::Invalid, resolveHaTokenInput(false, ""));
  TEST_ASSERT_EQUAL(CredentialAction::Invalid, resolveHaTokenInput(false, nullptr));
  TEST_ASSERT_EQUAL(CredentialAction::Replace, resolveHaTokenInput(false, "synthetic-token"));
  TEST_ASSERT_EQUAL(CredentialAction::Keep, resolveHaTokenInput(true, ""));
  TEST_ASSERT_EQUAL(CredentialAction::Replace, resolveHaTokenInput(true, "replacement-token"));

  TEST_ASSERT_EQUAL(CredentialAction::Clear, resolveOtaInput(false, false, "", 65));
  TEST_ASSERT_EQUAL(CredentialAction::Clear, resolveOtaInput(false, true, "ignored-password", 65));
  TEST_ASSERT_EQUAL(CredentialAction::Invalid, resolveOtaInput(true, false, "", 65));
  TEST_ASSERT_EQUAL(CredentialAction::Keep, resolveOtaInput(true, true, "", 65));
  TEST_ASSERT_EQUAL(CredentialAction::Replace, resolveOtaInput(true, false, "twelve-chars", 65));
  TEST_ASSERT_EQUAL(CredentialAction::Invalid, resolveOtaInput(true, false, "too-short", 65));

  char unterminated[13];
  memset(unterminated, 'x', sizeof(unterminated));
  TEST_ASSERT_FALSE(isValidOtaPassword(unterminated, sizeof(unterminated)));
  TEST_ASSERT_FALSE(isValidOtaPassword(nullptr, 65));
  TEST_ASSERT_TRUE(isValidMd5Hash("0123456789abcdef0123456789ABCDEF"));
  TEST_ASSERT_FALSE(isValidMd5Hash("0123456789abcdef0123456789abcde"));
  TEST_ASSERT_FALSE(isValidMd5Hash("0123456789abcdef0123456789abcdeg"));
}

int main(int argc, char **argv)
{
  UNITY_BEGIN();
  RUN_TEST(testProgressBarWidth);
  RUN_TEST(testDeadlinesAndElapsedTime);
  RUN_TEST(testHttpOrigins);
  RUN_TEST(testSameOriginPolicy);
  RUN_TEST(testSha256FingerprintNormalization);
  RUN_TEST(testRedirectPolicy);
  RUN_TEST(testCredentialPolicy);
  return UNITY_END();
}
