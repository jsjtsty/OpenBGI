# Route dependency downloads (git clone, file(DOWNLOAD)) through OPENBGI_PROXY when set.
if(OPENBGI_PROXY)
  foreach(var http_proxy https_proxy HTTP_PROXY HTTPS_PROXY)
    set(ENV{${var}} "${OPENBGI_PROXY}")
  endforeach()
  message(STATUS "Dependency downloads use proxy ${OPENBGI_PROXY}")
endif()
