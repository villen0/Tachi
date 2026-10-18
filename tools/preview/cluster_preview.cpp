// Host preview of src/cluster.h: renders the rally cluster page for a few sample situations.
// Build: g++ -O2 -std=c++17 -I../../src cluster_preview.cpp -o cluster_preview   Run: ./cluster_preview out_dir
#include "cluster.h"
#include "mockgfx.h"

int main(int argc, char **argv) {
  std::string out = argc > 1 ? argv[1] : "out";
  if (system(("mkdir -p " + out).c_str())) return 1;
  struct Case { const char *name; int rpm, mph; } cases[] = {{"idle", 780, 0}, {"cruise", 2300, 46}, {"pull", 4400, 38}, {"redline", 6600, 62}};
  for (auto &c : cases) {
    MockGfx g(cluster::W, cluster::H);
    cluster::Data d; d.rpm = c.rpm; d.mph = c.mph; d.live = true; d.rate = 5;
    d.haveCool = d.haveIntake = d.haveFuel = d.haveBatt = d.haveLoad = true;
    d.coolF = 198; d.intakeF = 88; d.fuelPct = 62; d.battV = 14.1f; d.loadPct = c.rpm > 4000 ? 78 : 24; d.runSec = 312;
    cluster::draw(&g, d, 1000);
    savePPM(g, out + "/c_" + c.name + ".ppm");
  }
  return 0;
}
