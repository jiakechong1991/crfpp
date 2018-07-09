//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: path.h 1595 2007-02-24 10:18:32Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#ifndef CRFPP_PATH_H_
#define CRFPP_PATH_H_

#include <vector>
#include "node.h"

namespace CRFPP {
struct Node;

struct Path {
  Node      *rnode;  // 边的右连接点
  Node      *lnode;  // 边的左连接点
  const int *fvector; // 把这个cur(x轴) 对应的字特征函数集，绑定到这个边上，方便使用
  double     cost;  // 计算边的代价 cost_factor_*∑(w*f)


  Path() : rnode(0), lnode(0), fvector(0), cost(0.0) {}

  // for CRF
  void calcExpectation(double *expected, double, size_t) const;
  void add(Node *_lnode, Node *_rnode) ;

  void clear() {
    rnode = lnode = 0;
    fvector = 0;
    cost = 0.0;
  }
};

typedef std::vector<Path*>::const_iterator const_Path_iterator;
}
#endif
