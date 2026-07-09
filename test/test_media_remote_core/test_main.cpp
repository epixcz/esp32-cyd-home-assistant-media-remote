#include <MediaRemoteCore.h>
#include <unity.h>

#include <stdint.h>

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

int main(int argc, char **argv)
{
  UNITY_BEGIN();
  RUN_TEST(testProgressBarWidth);
  RUN_TEST(testDeadlinesAndElapsedTime);
  RUN_TEST(testHttpOrigins);
  RUN_TEST(testSameOriginPolicy);
  return UNITY_END();
}
