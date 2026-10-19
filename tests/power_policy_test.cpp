#include "power_policy.h"
#include <cassert>
#include <iostream>
using namespace PowerPolicy;
int main() {
  GpsSchedule gps;
  gps.set(true, 100);
  assert(gps.wanted(101, true, GpsMode::Auto, false));
  assert(!gps.wanted(101, false, GpsMode::On, false));
  assert(!gps.wanted(101, true, GpsMode::Off, false));
  assert(gps.wanted(10099, true, GpsMode::Auto, true));
  assert(!gps.wanted(10100, true, GpsMode::Auto, true));
  assert(gps.wanted(180099, true, GpsMode::Auto, false));
  assert(!gps.wanted(180100, true, GpsMode::Auto, false));
  assert(gps.wanted(180100, true, GpsMode::On, false));
  gps.set(false, 180100);
  assert(!gps.wanted(1080099, true, GpsMode::Auto, false));
  assert(gps.wanted(1080100, true, GpsMode::Auto, false));
  gps.set(false, UINT32_MAX - 1000);
  assert(!gps.wanted(2000, true, GpsMode::Auto, false));
  assert(gps.wanted(900000, true, GpsMode::Auto, false));
  assert(!lightExpired(true, 59999, 0));
  assert(lightExpired(true, 60000, 0));
  assert(!lightExpired(false, 60000, 0));
  assert(lightExpired(true, 60000, UINT32_MAX - 1000));
  std::cout << "GPS sample/timeout/switch/override and rollover tests passed\n";
}
