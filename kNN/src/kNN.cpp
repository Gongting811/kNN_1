
#include <Rcpp.h>
#include <stdlib.h>
#include <R.h>
#include <math.h>
#include <float.h>
#include <thread>
#include <array>
#include <mutex>

using namespace Rcpp;
using namespace std;

#define EPS 1e-4       

#define UNIF_RAND unif_rand() // random generator
#define MAX_TIES 1000 // maximum of allowed ties for distance comparison
#define NUM_THREAD 3 // default number of threads
#define MAX_VALUE 0.99 * DOUBLE_XMAX // possiblt max value for distance computation and comparison

mutex myMutex; // initialize mutex object for data protection in multi-thread

// [[Rcpp::export]]
void C_knn_search (int &k_in, int &l_in, int &ntrain, int &dim, NumericMatrix &train, 
                 IntegerVector &cls, NumericMatrix &test, IntegerVector &res, NumericVector &pr, 
                 int &ncls, int &cv, int &use_all, int begin, int end) {
  int i, index, j, k, k1, k_init = k_in, k_pt, max_vote, n_t, nties, extras;
  int pos[MAX_TIES], nclass[MAX_TIES];
  int j1, j2, needed, ti;
  double dist, diff, dists[MAX_TIES];
  IntegerVector votes(ncls + 1);
  
  GetRNGstate(); 
  
  for (n_t = begin; n_t < end; n_t++) {
    k_pt = k_init;
    // Initialize dists
    for (k = 0; k < k_pt; k++)
      dists[k] = MAX_VALUE;
    
    for (j = 0; j < ntrain; j++) {
      if ((cv > 0) && (j == n_t))
        continue;
      // compute square Euclidean distance between j-th train sample and n_t-th test sample
      dist = 0.0;
      for (k = 0; k < dim; k++) {
        diff = test(n_t, k) - train(j, k);
        dist += diff * diff;
      }
      
      // insert the distance into dists if it's currently within k smallest
      if (dist <= dists[k_init - 1] * (1 + EPS))
        for (k = 0; k <= k_pt; k++)
          if (dist < dists[k]) {
            for (k1 = k_pt; k1 > k; k1--) {
              dists[k1] = dists[k1 - 1];
              pos[k1] = pos[k1 - 1];
            }
            dists[k] = dist;
            pos[k] = j;
            if (dists[k_pt] <= dists[k_init - 1])
              if (++k_pt == MAX_TIES - 1)
                throw std::range_error("There are too many ties in k nearest neighbors!");
              break;
          }
          dists[k_pt] = MAX_VALUE;
    }
    // k cloest samples have been selected. Then start counting votes
    std::lock_guard<std::mutex> myLock(myMutex); // lock data to prevent disorder
    
    // Initialize votes count vector
    for (j = 0; j <= ncls; j++)
      votes[j] = 0;
    
    // use_all is true, "all distances equal to the kth largest are included"
    if (use_all) {
      for (j = 0; j < k_init; j++) {
        votes[cls[pos[j]]]++;
      }
      extras = 0;
      for (j = k_init; j < k_pt; j++) {
        if (dists[j] <= dists[k_init - 1] * (1 + EPS)){
          extras++;
          votes[cls[pos[j]]]++;
        }
      }
    } 
    /* use_all is false, "a random selection of distances equal to the kth 
     is chosen to use exactly k neighbours." */
    else { 
      extras = 0;
      for (j = 0; j < k_init; j++) {
        if (dists[j] >= dists[k_init - 1] * (1 - EPS))
          break;
        votes[cls[pos[j]]]++;
      }
      j1 = j;
      if (j1 == k_init - 1) { // no ties for largest
        votes[cls[pos[j1]]]++;
      } 
      else {
        j1 = j;
        needed = k_init - j1;
        for (j = 0; j < needed; j++)
          nclass[j] = cls[pos[j1 + j]];
        ti = needed;
        for (j = j1 + needed; j < k_pt; j++) {
          if (dists[j] > dists[k_init - 1] * (1 + EPS))
            break;
          if (++ti * UNIF_RAND < needed) {
            j2 = j1 + (int) (UNIF_RAND * needed);
            nclass[j2] = cls[pos[j]];
          }
        }
        for (j = 0; j < needed; j++)
          votes[nclass[j]]++;
      }
    }

    nties = 1;
    index = 0;
    max_vote = (l_in > 0) ? (l_in - 1 + extras) : 0;
    
    for (i = 1; i <= ncls; i++)
      if (votes[i] > max_vote) { //select larger vote
        nties = 1;
        index = i;
        max_vote = votes[i];
      } 
      // if there is tie, select later one
      else if ((votes[i] == max_vote && votes[i] >= l_in) && (++nties * UNIF_RAND < 1.0)) {
          index = i;
      }
    res[n_t] = index;
    pr[n_t] = (double) max_vote / (k_init + extras);
  }
  PutRNGstate();
}

// [[Rcpp::export]]
List C_kNN_multi_thread(int &k_in, int &l_in, int &ntrain, int &dim, 
            NumericMatrix &train, IntegerVector &cls, NumericMatrix &test, 
            int &ncls, int &cv, int &use_all) {
  int ntest = test.nrow(), begin, end;
  int div = ceil(ntest / NUM_THREAD);
  IntegerVector res(ntest); 
  NumericVector pr(ntest); 
  
  array<thread, NUM_THREAD> threads;
  for (int thread_id = 0; thread_id < NUM_THREAD; thread_id++) {
    begin = thread_id*div;
    end = begin+div;
    if (thread_id == NUM_THREAD - 1)
      end = ntest;
    threads[thread_id] = thread(C_knn_search, ref(k_in), ref(l_in), ref(ntrain), ref(dim), 
                          ref(train), ref(cls), ref(test), ref(res), ref(pr), 
                          ref(ncls), ref(cv), ref(use_all), begin, end);
  }
  for( auto & t : threads ) {
    t.join();
  }
  
  return List::create( res, pr );
}
