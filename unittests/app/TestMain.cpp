#define CATCH_CONFIG_RUNNER
#include "core/AppConfig.h"
#include <QApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  
  // Initialize AppConfig with default settings for testing
  acav::AppConfig::initialize();
  
  return Catch::Session().run(argc, argv);
}

