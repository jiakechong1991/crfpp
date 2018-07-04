//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: crf_learn.cpp 1587 2007-02-12 09:00:36Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#include "crfpp.h"
#include "winmain.h"

int main(int argc, char **argv) {  // 这是入口函数
  return crfpp_learn(argc, argv);
}

// crf_learn -f 3 -c 4.0 template train.data model
