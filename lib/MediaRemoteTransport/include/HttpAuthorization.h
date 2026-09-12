#pragma once
#include <MediaRemoteCore.h>
#include <string>
namespace media_remote {
template<class Http>
void addHaAuthorization(Http &http, const char *haUrl, const char *requestUrl, const char *token)
{
  if (sameOrigin(haUrl, requestUrl)) {
    std::string authorization = std::string("Bearer ") + token;
    http.addHeader("Authorization", authorization.c_str());
  }
}
}
