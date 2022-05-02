#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "uthread.h"
#include "uthread_mutex_cond.h"

#define NUM_ITERATIONS 1000

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__)
#else
#define VERBOSE_PRINT(S, ...) ((void) 0) // do nothing
#endif
/**
 * You might find these declarations helpful.
 *   Note that Resource enum had values 1, 2 and 4 so you can combine resources;
 *   e.g., having a MATCH and PAPER is the value MATCH | PAPER == 1 | 2 == 3
 */
enum Resource            {    MATCH = 1, PAPER = 2,   TOBACCO = 4};
char* resource_name [] = {"", "match",   "paper", "", "tobacco"};

// # of threads waiting for a signal. Used to ensure that the agent
// only signals once all other threads are ready.
int num_active_threads = 0;

int signal_count [5];  // # of times resource signalled
int smoke_count  [5];  // # of times smoker with resource smoked
uthread_cond_t tobacco_go, paper_go, match_go, checker_go, try, smoke;
uthread_mutex_t mux;
int sum = 0;
int matchValue = 1;
int paperValue = 2;
int tobaccoValue = 4;

struct Agent {
  uthread_mutex_t mutex;
  uthread_cond_t  match;
  uthread_cond_t  paper;
  uthread_cond_t  tobacco;
  uthread_cond_t  smoke;
};

struct Smoker {
  uthread_mutex_t mutex;
  uthread_cond_t materials;
};

struct Smoker * createSmoker() {
  struct Smoker * smoker = malloc(sizeof (struct Smoker));
  return smoker;
}

struct Agent* createAgent() {
  struct Agent* agent = malloc (sizeof (struct Agent));
  agent->mutex   = uthread_mutex_create();
  agent->paper   = uthread_cond_create(agent->mutex);
  agent->match   = uthread_cond_create(agent->mutex);
  agent->tobacco = uthread_cond_create(agent->mutex);
  agent->smoke   = uthread_cond_create(agent->mutex);
  return agent;
}



/**
 * This is the agent procedure.  It is complete and you shouldn't change it in
 * any material way.  You can modify it if you like, but be sure that all it does
 * is choose 2 random resources, signal their condition variables, and then wait
 * wait for a smoker to smoke.
 */
void* agent (void* av) {
  struct Agent* a = av;
  static const int choices[]         = {MATCH|PAPER, MATCH|TOBACCO, PAPER|TOBACCO};
  static const int matching_smoker[] = {TOBACCO,     PAPER,         MATCH};

  srandom(time(NULL));
  
  uthread_mutex_lock (a->mutex);
  // Wait until all other threads are waiting for a signal
  while (num_active_threads < 3)
    uthread_cond_wait (a->smoke);

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int r = random() % 6;
    switch(r) {
    case 0:
      signal_count[TOBACCO]++;
      VERBOSE_PRINT ("match available\n");
      uthread_cond_signal (a->match);
      VERBOSE_PRINT ("paper available\n");
      uthread_cond_signal (a->paper);
      break;
    case 1:
      signal_count[PAPER]++;
      VERBOSE_PRINT ("match available\n");
      uthread_cond_signal (a->match);
      VERBOSE_PRINT ("tobacco available\n");
      uthread_cond_signal (a->tobacco);
      break;
    case 2:
      signal_count[MATCH]++;
      VERBOSE_PRINT ("paper available\n");
      uthread_cond_signal (a->paper);
      VERBOSE_PRINT ("tobacco available\n");
      uthread_cond_signal (a->tobacco);
      break;
    case 3:
      signal_count[TOBACCO]++;
      VERBOSE_PRINT ("paper available\n");
      uthread_cond_signal (a->paper);
      VERBOSE_PRINT ("match available\n");
      uthread_cond_signal (a->match);
      break;
    case 4:
      signal_count[PAPER]++;
      VERBOSE_PRINT ("tobacco available\n");
      uthread_cond_signal (a->tobacco);
      VERBOSE_PRINT ("match available\n");
      uthread_cond_signal (a->match);
      break;
    case 5:
      signal_count[MATCH]++;
      VERBOSE_PRINT ("tobacco available\n");
      uthread_cond_signal (a->tobacco);
      VERBOSE_PRINT ("paper available\n");
      uthread_cond_signal (a->paper);
      break;
    }
    VERBOSE_PRINT ("agent is waiting for smoker to smoke\n");
    uthread_cond_wait (a->smoke);
  }
  
  uthread_mutex_unlock (a->mutex);
  return NULL;
}

// waits for a signal from the material checkers and then, if two materials have been recieved signal the correct smoker
void checker() {
  if (sum == matchValue + paperValue) {
    uthread_cond_signal(tobacco_go);
    sum = 0;
  } else if (sum == matchValue + tobaccoValue) {
    uthread_cond_signal(paper_go);
    sum = 0;
  } else if (sum == paperValue + tobaccoValue) {
    uthread_cond_signal(match_go);
    sum = 0;
  }
}

// ############ SMOKERS #############
// wait for signal tobacco_go, match_go or paper_go and when recieved, smoke then wait again.
void* tobacco_smoker (void *av) {
  struct Smoker * smoker = av;
  uthread_mutex_lock(mux);
  while (1) {
    uthread_cond_wait(smoker->materials);
    uthread_cond_signal(smoke);
    
    smoke_count[TOBACCO] += 1;

  }
  uthread_mutex_unlock(mux);
  return NULL;
}
void* paper_smoker (void *av) {
  struct Smoker * smoker = av;
  uthread_mutex_lock(mux);
  while (1) {
    uthread_cond_wait(smoker->materials);
    uthread_cond_signal(smoke);
    
    smoke_count[PAPER] += 1;
  }
  uthread_mutex_unlock(mux);
  return NULL;
}
void* matches_smoker (void *av) {
  struct Smoker * smoker = av;
  uthread_mutex_lock(mux);
  while (1) {
    uthread_cond_wait(smoker->materials);
    uthread_cond_signal(smoke);

    smoke_count[MATCH] += 1;

  }
  uthread_mutex_unlock(mux);
  return NULL;
}

// ########### CHECKERS #############
// wait for signal from agent and then try to make a smoker smoke by signaling try (checker func)
void * paper_checker (void * av) {
  struct Agent * a = av;
  uthread_mutex_lock(mux);
  while (1) {
    uthread_cond_wait(a->paper);
    sum += paperValue;
    checker();
  }
  uthread_mutex_unlock(mux);
  return NULL;
}

void * tobacco_checker (void * av) {
  struct Agent * a = av;
  uthread_mutex_lock(mux);
  while (1) {
    uthread_cond_wait(a->tobacco);
    sum += tobaccoValue;
    checker();
  }
  uthread_mutex_unlock(mux);
  return NULL;
}

void * matches_checker (void * av) {
  struct Agent * a = av;
  uthread_mutex_lock(mux);
  while (1) {
    uthread_cond_wait(a->match);
    sum += matchValue;
    checker();
  }
  uthread_mutex_unlock(mux);
  return NULL;
}

int main (int argc, char** argv) {
  
  struct Agent* a = createAgent();
  uthread_t agent_thread;
  tobacco_go = uthread_cond_create(a->mutex);
  paper_go = uthread_cond_create(a->mutex);
  match_go = uthread_cond_create(a->mutex);
  checker_go = uthread_cond_create(a->mutex);
  try = uthread_cond_create(a->mutex);
  mux = a->mutex;
  smoke = a->smoke;

  uthread_init(7);
  
  struct Smoker * tobacco = createSmoker();
  tobacco->mutex = a->mutex;
  tobacco->materials = tobacco_go;

  struct Smoker * paper = createSmoker();
  paper->mutex = a->mutex;
  paper->materials = paper_go;

  struct Smoker * match = createSmoker();
  match->mutex = a->mutex;
  match->materials = match_go;
  
  uthread_t tobacco_thread, paper_thread, match_thread, tobacco_check_thread, match_check_thread, paper_check_thread, checker_thread;

  tobacco_thread = uthread_create(tobacco_smoker, tobacco);
  paper_thread = uthread_create(paper_smoker, paper);
  match_thread = uthread_create(matches_smoker, match);
  
  tobacco_check_thread = uthread_create(tobacco_checker, a);
  paper_check_thread = uthread_create(paper_checker, a);
  match_check_thread = uthread_create(matches_checker, a);

  num_active_threads = 6;



  agent_thread = uthread_create(agent, a);
  uthread_join(agent_thread, NULL);

  assert (signal_count [MATCH]   == smoke_count [MATCH]);
  assert (signal_count [PAPER]   == smoke_count [PAPER]);
  assert (signal_count [TOBACCO] == smoke_count [TOBACCO]);
  assert (smoke_count [MATCH] + smoke_count [PAPER] + smoke_count [TOBACCO] == NUM_ITERATIONS);

  printf ("Smoke counts: %d matches, %d paper, %d tobacco\n",
          smoke_count [MATCH], smoke_count [PAPER], smoke_count [TOBACCO]);
  return 0;
}
