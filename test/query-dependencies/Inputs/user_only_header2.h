// user_only_header2.h - Another user header that includes user_only_header1.h
#pragma once

#include "user_only_header1.h"

class DataProcessor {
public:
  void process(Data& data);
private:
  int count_;
};
