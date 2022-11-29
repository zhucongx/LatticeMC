#include "KineticMcAbstract.h"
#include <utility>
#include <chrono>
#include <mpi.h>
namespace mc {

KineticMcFirstAbstract::KineticMcFirstAbstract(cfg::Config config,
                                               const unsigned long long int log_dump_steps,
                                               const unsigned long long int config_dump_steps,
                                               const unsigned long long int maximum_steps,
                                               const unsigned long long int thermodynamic_averaging_steps,
                                               const unsigned long long int restart_steps,
                                               const double restart_energy,
                                               const double restart_time,
                                               const double temperature,
                                               const std::set<Element> &element_set,
                                               const std::string &json_coefficients_filename)
    : McAbstract(std::move(config),
                 log_dump_steps,
                 config_dump_steps,
                 maximum_steps,
                 thermodynamic_averaging_steps,
                 restart_steps,
                 restart_energy,
                 restart_time,
                 temperature,
                 element_set,
                 json_coefficients_filename,
                 "lkmc_log.txt"),
      energy_predictor_(json_coefficients_filename,
                        config_,
                        element_set,
                        100000),
      vacancy_lattice_id_(config_.GetVacancyLatticeId()) {
}
KineticMcFirstAbstract::~KineticMcFirstAbstract() = default;

void KineticMcFirstAbstract::Dump() const {
  if (world_rank_ != 0) {
    return;
  }
  if (steps_ == 0) {
    config_.WriteLattice("lattice.txt");
    config_.WriteElement("element.txt");
    ofs_ << "steps\ttime\tenergy\taverage_energy\tabsolute_energy\tEa\tdE\ttype" << std::endl;
  }
  if (steps_ % config_dump_steps_ == 0) {
    config_.WriteMap("map" + std::to_string(steps_) + ".txt");
    config_.WriteConfig(std::to_string(steps_) + ".cfg", false);
  }
  unsigned long long int log_dump_steps;
  if (steps_ > 10 * log_dump_steps_) {
    log_dump_steps = log_dump_steps_;
  } else {
    log_dump_steps = static_cast<unsigned long long int>(
        std::pow(10, static_cast<unsigned long long int>(std::log10(
            steps_ + 1) - 1)));
    log_dump_steps = std::max(log_dump_steps, static_cast<unsigned long long int>(1));
    log_dump_steps = std::min(log_dump_steps, log_dump_steps_);
  }
  if (steps_ % log_dump_steps == 0) {
    ofs_ << steps_ << '\t' << time_ << '\t' << energy_ << '\t'
         << thermodynamic_averaging_.GetThermodynamicAverage(beta_) << '\t'
         << initial_absolute_energy_ + energy_ << '\t'
         << event_k_i_.GetForwardBarrier() << '\t'
         << event_k_i_.GetEnergyChange() << '\t'
         << config_.GetElementAtLatticeId(event_k_i_.GetIdJumpPair().first).GetString()
         << std::endl;
  }
}
size_t KineticMcFirstAbstract::SelectEvent() const {
  const double random_number = unit_distribution_(generator_);
  auto it = std::lower_bound(event_k_i_list_.begin(),
                             event_k_i_list_.end(),
                             random_number,
                             [](const auto &lhs, double value) {
                               return lhs.GetCumulativeProvability() < value;
                             });
  // If not find (maybe generated 1), which rarely happens, returns the last event
  if (it == event_k_i_list_.cend()) {
    it--;
  }
  if (world_size_ > 1) {
    int event_id = static_cast<int>(it - event_k_i_list_.cbegin());
    MPI_Bcast(&event_id, 1, MPI_INT, 0, MPI_COMM_WORLD);
    return static_cast<size_t>(event_id);
  } else {
    return static_cast<size_t>(it - event_k_i_list_.cbegin());
  }
}
void KineticMcFirstAbstract::OneStepSimulation() {
  thermodynamic_averaging_.AddEnergy(energy_);
  if (is_restarted_) {
    is_restarted_ = false;
  } else {
    Dump();
  }
  BuildEventList();
  time_ += CalculateTime();
  event_k_i_ = event_k_i_list_[SelectEvent()];
  energy_ += event_k_i_.GetEnergyChange();
  config_.LatticeJump(event_k_i_.GetIdJumpPair());
  ++steps_;
  vacancy_lattice_id_ = event_k_i_.GetIdJumpPair().second;
}
void KineticMcFirstAbstract::Simulate() {
  while (steps_ <= maximum_steps_) {
    OneStepSimulation();
  }
}

KineticMcChainAbstract::KineticMcChainAbstract(cfg::Config config,
                                               const unsigned long long int log_dump_steps,
                                               const unsigned long long int config_dump_steps,
                                               const unsigned long long int maximum_steps,
                                               const unsigned long long int thermodynamic_averaging_steps,
                                               const unsigned long long int restart_steps,
                                               const double restart_energy,
                                               const double restart_time,
                                               const double temperature,
                                               const std::set<Element> &element_set,
                                               const std::string &json_coefficients_filename)
    : KineticMcFirstAbstract(std::move(config),
                             log_dump_steps,
                             config_dump_steps,
                             maximum_steps,
                             thermodynamic_averaging_steps,
                             restart_steps,
                             restart_energy,
                             restart_time,
                             temperature,
                             element_set,
                             json_coefficients_filename),
      previous_j_lattice_id_(config_.GetFirstNeighborsAdjacencyList()[vacancy_lattice_id_][0]) {
  MPI_Op_create(DataSum, 1, &mpi_op_);
  DefineStruct(&mpi_datatype_);
}
void KineticMcChainAbstract::OneStepSimulation() {
  KineticMcFirstAbstract::OneStepSimulation();
  previous_j_lattice_id_ = event_k_i_.GetIdJumpPair().first;
}
KineticMcChainAbstract::~KineticMcChainAbstract() {
  MPI_Op_free(&mpi_op_);
  MPI_Type_free(&mpi_datatype_);
}

}
// mc