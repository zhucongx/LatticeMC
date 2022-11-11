#include "Iterator.h"

#include <utility>
namespace ansys {

Iterator::Iterator(unsigned long long int initial_steps,
                   unsigned long long int increment_steps,
                   Element solvent_element,
                   std::set<Element> element_set,
                   size_t smallest_cluster_criteria,
                   size_t solvent_bond_criteria,
                   const std::string &predictor_filename,
                   std::string log_type,
                   std::string config_type)
    : initial_steps_(initial_steps),
      increment_steps_(increment_steps),
      final_number_(increment_steps),
      solvent_element_(solvent_element),
      smallest_cluster_criteria_(smallest_cluster_criteria),
      solvent_bond_criteria_(solvent_bond_criteria),
      energy_estimator_(predictor_filename, std::move(element_set)),
      log_type_(std::move(log_type)),
      config_type_(std::move(config_type)) {
  std::string log_file_name;
  if (log_type_ == "kinetic_mc") {
    log_file_name = "kmc_log.txt";
  } else if (log_type_ == "canonical_mc") {
    log_file_name = "cmc_log.txt";
  } else if (log_type_ == "canonical_mc_step_t") {
    log_file_name = "cmc_log.txt";
  } else {
    std::cerr << "Unknown log type: " << log_type_ << std::endl;
  }
  std::ifstream ifs(log_file_name, std::ifstream::in);
  if (!ifs.is_open()) {
    std::cerr << "Cannot open " << log_file_name << "\n";
    return;
  }
  unsigned long long step_number;
  double energy, time, temperature;
  while (ifs.peek() != '0') {
    ifs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }

  while (true) {
    if (ifs.eof() || ifs.bad()) {
      break;
    }
    if (ifs.peek() == '#') {
      ifs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }
    if (log_type_ == "kinetic_mc") {
      ifs >> step_number >> time >> energy;
    } else if (log_type_ == "canonical_mc") {
      ifs >> step_number >> energy;
    } else if (log_type_ == "canonical_mc_step_t") {
      ifs >> step_number >> energy >> temperature;
    } else {
      std::cerr << "Unknown log type: " << log_type_ << std::endl;
    }

    ifs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    if (step_number >= initial_steps_ && (step_number - initial_steps_) % increment_steps == 0) {
      if (log_type_ == "kinetic_mc") {
        filename_info_hashset_[step_number] = time;
      } else if (log_type_ == "canonical_mc") {
        filename_info_hashset_[step_number] = 0;
      } else if (log_type_ == "canonical_mc_step_t") {
        filename_info_hashset_[step_number] = temperature;
      } else {
        std::cerr << "Unknown log type: " << log_type_ << std::endl;
      }
      final_number_ = step_number;
    }
  }
  // final_number_ -= increment_steps;
}
Iterator::~Iterator() = default;
void Iterator::RunCluster() const {
  // start
  std::ofstream ofs("cluster_info.json", std::ofstream::out);
  ofs << "[ \n";

  for (unsigned long long i = 0; i <= final_number_; i += increment_steps_) {
    std::cout << i << " / " << final_number_ << std::endl;

    cfg::Config config;
    if (config_type_ == "config") {
      config = cfg::Config::ReadCfg(std::to_string(i) + ".cfg");
      config.ReassignLatticeVector();
    } else if (config_type_ == "map") {
      config = cfg::Config::ReadMap("lattice.txt",
                                    "element.txt",
                                    "map" + std::to_string(i) + ".txt");
    } else {
      throw std::invalid_argument("Unknown config type: " + config_type_);
    }
    ClustersFinder cluster_finder(config,
                                  solvent_element_,
                                  smallest_cluster_criteria_,
                                  solvent_bond_criteria_,
                                  energy_estimator_);
    auto num_different_element =
        cluster_finder.FindClustersAndOutput("cluster", std::to_string(i) + "_cluster.cfg");

    ofs << "{ \n"
        << "\"index\" : "
        << "\"" << std::to_string(i) << "\",\n"
        << "\"info\" : " << filename_info_hashset_.at(i) << ",\n"
        << "\"clusters\" : [ \n";
    for (auto it = num_different_element.cbegin(); it < num_different_element.cend(); ++it) {
      ofs << "[ ";
      const auto &cluster = *it;
      std::for_each(cluster.first.cbegin(), cluster.first.cend(), [&ofs](auto ii) {
        ofs << ii.second << ", ";
      });
      ofs << std::setprecision(16) << cluster.second;
      if (it == num_different_element.cend() - 1) {
        ofs << "] \n";
      } else {
        ofs << "], \n";
      }
    }
    ofs << "]\n";
    if (i != final_number_) {
      ofs << "}, \n";
    } else {
      ofs << "} \n";
    }
  }
  ofs << " ]" << std::endl;
}
void Iterator::RunReformat() const {
  if (config_type_ == "map") {
    std::cerr << "Reformat map already done" << std::endl;
    return;
  }
  for (unsigned long long i = 0; i <= final_number_; i += increment_steps_) {
    std::cout << i << " / " << final_number_ << std::endl;
    auto config = cfg::Config::ReadCfg(std::to_string(i) + ".cfg");
    config.ReassignLatticeVector();
    if (i == 0) {
      config.WriteLattice("lattice.txt");
      config.WriteElement("element.txt");
    }
    config.WriteMap("map" + std::to_string(i) + ".txt");
  }
}
} // namespace ansys