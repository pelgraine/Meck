// WebReaderDeps.cpp
// -----------------------------------------------------------------------
// PlatformIO library dependency finder (LDF) hint file.
//
// The web reader's WiFi/HTTP includes live in WebReaderScreen.h (header-only),
// but PlatformIO's LDF can't always trace framework library dependencies
// through conditional #include chains in headers. This .cpp file exposes
// the includes at the top level where the scanner reliably finds them.
//
// No actual code here — just #include directives for the dependency finder.
// -----------------------------------------------------------------------
#ifdef MECK_WEB_READER
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#endif