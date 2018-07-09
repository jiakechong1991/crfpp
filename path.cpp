//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: path.cpp 1587 2007-02-12 09:00:36Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#include <cmath>
#include "path.h"
#include "common.h"

namespace CRFPP {

void Path::calcExpectation(double *expected, double Z, size_t size) const {
  const double c = std::exp(lnode->alpha + cost + rnode->beta - Z);
  for (const int *f = fvector; *f != -1; ++f) {
    expected[*f + lnode->y * size + rnode->y] += c;
  }
}

void Path::add(Node *_lnode, Node *_rnode) {
  lnode = _lnode;  // 设置边的 左连接点
  rnode = _rnode;  // 设置边的 右连接点
  lnode->rpath.push_back(this);  // 设置 左连接点 的 右连接边
  rnode->lpath.push_back(this);  // 设置 右连接点 的 左连接边
}
}
