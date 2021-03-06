// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//

#include <actuator/ActuatorParsing.h>
#include <actuator/ActuatorBulk.h>
#include <actuator/ActuatorInfo.h>
#include <stdexcept>
#include <NaluParsing.h>

namespace sierra {
namespace nalu {

/*! \brief Parse parameters to construct meta data for actuators
 *  Parse parameters and construct meta data for actuators.
 *  Intent is to divorce object creation/memory allocation from parsing
 *  to facilitate device compatibility.
 *
 *  This also has the added benefit of increasing unittest-ability.
 *
 *  General parameters that apply to all actuators should be parsed here.
 *  More specific actuator methods (i.e. LineFAST, DiskFAST) should implement
 *  another parse function that takes one YAML::Node and one ActuatorMeta object
 *  as inputs and returns a more specialized ActuatorMeta object.
 */
ActuatorMeta
actuator_parse(const YAML::Node& y_node)
{
  const YAML::Node y_actuator = y_node["actuator"];
  ThrowErrorMsgIf(
    !y_actuator, "actuator argument is "
                 "missing from yaml node passed to actuator_parse");
  int nTurbines = 0;
  std::string actuatorTypeName;
  get_required(y_actuator, "type", actuatorTypeName);
  if ((ActuatorTypeMap[actuatorTypeName]==ActuatorType::ActLineSimpleNGP)||
      (ActuatorTypeMap[actuatorTypeName]==ActuatorType::ActLineSimple))
    {
      get_required(y_actuator, "n_simpleblades", nTurbines);
    } else {
    get_required(y_actuator, "n_turbines_glob", nTurbines);
  }
  ActuatorMeta actMeta(nTurbines, ActuatorTypeMap[actuatorTypeName]);
  // search specifications
  std::string searchMethodName = "na";
  get_if_present(
    y_actuator, "search_method", searchMethodName, searchMethodName);
  // determine search method for this pair
  if (searchMethodName == "boost_rtree") {
    actMeta.searchMethod_ = stk::search::KDTREE;
    NaluEnv::self().naluOutputP0()
      << "Warning: search method 'boost_rtree'"
      << " is being deprecated, switching to 'stk_kdtree'" << std::endl;
  } else if (searchMethodName == "stk_kdtree")
    actMeta.searchMethod_ = stk::search::KDTREE;
  else
    NaluEnv::self().naluOutputP0()
      << "Actuator::search method not declared; will use stk_kdtree"
      << std::endl;
  // extract the set of from target names; each spec is homogeneous in this
  // respect
  const YAML::Node searchTargets = y_actuator["search_target_part"];
  if (searchTargets) {
    if (searchTargets.Type() == YAML::NodeType::Scalar) {
      actMeta.searchTargetNames_.resize(1);
      actMeta.searchTargetNames_[0] = searchTargets.as<std::string>();
    } else {
      actMeta.searchTargetNames_.resize(searchTargets.size());
      for (size_t i = 0; i < searchTargets.size(); ++i) {
        actMeta.searchTargetNames_[i] = searchTargets[i].as<std::string>();
      }
    }
  } else {
    throw std::runtime_error("Actuator:: search_target_part is not declared.");
  }
  get_if_present_no_default(y_actuator, "fllt_correction", actMeta.useFLLC_);

  return actMeta;
}

} // namespace nalu
} // namespace sierra
