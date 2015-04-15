#include <iostream>
#include <chrono>
#include <boost/program_options.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include "storage/DiskVectorLMDB.hpp"
#include "LSH.hpp"
#include "Resorter.hpp"
#include "utils.hpp"
#include "lock.hpp"
#include "config.hpp"

using namespace std;
using namespace std::chrono;
namespace fs = boost::filesystem;
namespace po = boost::program_options;

long long getIndex(long long, int); // both must be 1 indexed

int main(int argc, char* argv[]) {
  
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "Show this help")
    ("datapath,d", po::value<string>()->required(),
     "Path to LMDB where the data is stored")
    ("imgslist,n", po::value<string>()->required(),
     "Filenames of all images in the corpus")
    ("ids2compute4", po::value<string>()->default_value(""),
     "File with indexes (1-indexed) of all images to be added to table")
    ("featcount,c", po::value<string>()->default_value(""),
     "File with list of number of features in each image."
     "NOT correspoding to above list, but for all images in global imgslist")
    ("load,l", po::value<string>(),
     "Path to load the initial hash table")
    ("save,s", po::value<string>(),
     "Path to save the hash table")
    ("nbits,b", po::value<int>()->default_value(250),
     "Number of bits in the representation")
    ("ntables,t", po::value<int>()->default_value(15),
     "Number of random proj tables in the representation")
    ("saveafter,a", po::value<int>()->default_value(1800), // every 1/2 hour
     "Time after which to snapshot the model (seconds)")
    ("printafter", po::value<int>()->default_value(5), // every 5 seconds
     "Time after which to print output (seconds)")
    ;

  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
  if (vm.count("help")) {
    cerr << desc << endl;
    return -1;
  }
  try {
    po::notify(vm);
  } catch(po::error& e) {
    cerr << e.what() << endl;
    return -1;
  }
  
  // read the list of images to hash
  int saveafter = vm["saveafter"].as<int>();
  int printafter = vm["printafter"].as<int>();
  vector<fs::path> imgslst;
  readList(vm["imgslist"].as<string>(), imgslst);
  vector<int> featcounts(imgslst.size(), 1); // default: 1 feat/image
  if (vm["featcount"].as<string>().length() > 0) {
    featcounts.clear();
    readList(vm["featcount"].as<string>(), featcounts);
  }
  vector<int> imgComputeIds;
  if (vm["ids2compute4"].as<string>().length() > 0) {
    readList(vm["ids2compute4"].as<string>(), imgComputeIds);
  } else {
    // all images
    for (int i = 1; i <= imgslst.size(); i++) {
      imgComputeIds.push_back(i);
    }
  }
  
  std::shared_ptr<LSH> l(new LSH(vm["nbits"].as<int>(), vm["ntables"].as<int>(), 9216));
  if (vm.count("load")) {
    ifstream ifs(vm["load"].as<string>(), ios::binary);
    boost::archive::binary_iarchive ia(ifs);
    ia >> *l;
    cout << "Loaded the search model for update" << endl;
  }
  vector<float> feat;
  
  high_resolution_clock::time_point last_print, last_save;
  last_print = last_save = high_resolution_clock::now();
  DiskVectorLMDB<vector<float>> tree(vm["datapath"].as<string>(), 1);

  if (l->lastLabelInserted >= 0) {
    cout << "Ignoring uptil (and including) " << l->lastLabelInserted 
         << ". Already exists in the index" << endl;
  }
  for (int meta_i = 0; meta_i < imgComputeIds.size(); meta_i++) {
    int i = imgComputeIds[meta_i] - 1; // hash this image
    for (int j = 0; j < featcounts[i]; j++) {
      long long idx = getIndex(i+1, j+1);
      if (l->lastLabelInserted >= idx) {
        continue;
      }
      if (!tree.Get(idx, feat)) break;
      l->insert(feat, idx);
    }
    high_resolution_clock::time_point now = high_resolution_clock::now();
    if (duration_cast<seconds>(now - last_print).count() >= printafter) {
      cout << "Done for " << meta_i + 1  << "/" << imgComputeIds.size()
           << " in " 
           << duration_cast<milliseconds>(now - last_print).count()
           << "ms" <<endl;
      last_print = now;
    }
    if (duration_cast<seconds>(now - last_save).count() >= saveafter) {
      if (vm.count("save")) {
        cout << "Saving model to " << vm["save"].as<string>() << "...";
        cout.flush();
        ofstream ofs(vm["save"].as<string>(), ios::binary);
        boost::archive::binary_oarchive oa(ofs);
        oa << *l;
        cout << "done." << endl;
      }
      last_save = now;
    }
  }

  if (vm.count("save")) {
    cout << "Saving model to " << vm["save"].as<string>() << "...";
    cout.flush();
    ofstream ofs(vm["save"].as<string>(), ios::binary);
    boost::archive::binary_oarchive oa(ofs);
    oa << *l;
    cout << "done." << endl;
  }

  return 0;
}

long long getIndex(long long imid, int pos) { // imid and pos must be 1 indexed
  return imid * MAXFEATPERIMG + pos;
}

