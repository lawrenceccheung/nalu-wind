// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//


#include "kernel/ScalarMassElemKernel.h"
#include "AlgTraits.h"
#include "master_element/MasterElement.h"
#include "master_element/MasterElementFactory.h"
#include "TimeIntegrator.h"
#include "SolutionOptions.h"

// template and scratch space
#include "BuildTemplates.h"
#include "ScratchViews.h"
#include "utils/StkHelpers.h"

// stk_mesh/base/fem
#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>

namespace sierra {
namespace nalu {

template<typename AlgTraits>
ScalarMassElemKernel<AlgTraits>::ScalarMassElemKernel(
  const stk::mesh::BulkData& bulkData,
  const SolutionOptions& solnOpts,
  ScalarFieldType* scalarQ,
  ElemDataRequests& dataPreReqs,
  const bool lumpedMass)
  : lumpedMass_(lumpedMass)
{
  // save off fields
  const stk::mesh::MetaData& metaData = bulkData.mesh_meta_data();

  scalarQN_ = scalarQ->field_of_state(stk::mesh::StateN).mesh_meta_data_ordinal();
  scalarQNp1_ = scalarQ->field_of_state(stk::mesh::StateNP1).mesh_meta_data_ordinal();
  if (scalarQ->number_of_states() == 2)
    scalarQNm1_ = scalarQN_;
  else
    scalarQNm1_ = scalarQ->field_of_state(stk::mesh::StateNM1).mesh_meta_data_ordinal();

  ScalarFieldType* density = metaData.get_field<ScalarFieldType>(
    stk::topology::NODE_RANK, "density");
  densityN_ = density->field_of_state(stk::mesh::StateN).mesh_meta_data_ordinal();
  densityNp1_ = density->field_of_state(stk::mesh::StateNP1).mesh_meta_data_ordinal();

  if (density->number_of_states() == 2)
    densityNm1_ = densityN_;
  else
    densityNm1_ = density->field_of_state(stk::mesh::StateNM1).mesh_meta_data_ordinal();
  coordinates_ = get_field_ordinal(metaData, solnOpts.get_coordinates_name());

  dataPreReqs.add_coordinates_field(get_field_ordinal(metaData, solnOpts.get_coordinates_name()),
                                    AlgTraits::nDim_, CURRENT_COORDINATES);

  meSCV_ = sierra::nalu::MasterElementRepo::get_volume_master_element<AlgTraits>();
  dataPreReqs.add_cvfem_volume_me(meSCV_);

  dataPreReqs.add_master_element_call(
      (lumpedMass_ ? SCV_SHIFTED_SHAPE_FCN : SCV_SHAPE_FCN), CURRENT_COORDINATES);

  // fields and data
  dataPreReqs.add_coordinates_field(coordinates_, AlgTraits::nDim_, CURRENT_COORDINATES);
  dataPreReqs.add_gathered_nodal_field(scalarQNm1_, 1);
  dataPreReqs.add_gathered_nodal_field(scalarQN_, 1);
  dataPreReqs.add_gathered_nodal_field(scalarQNp1_, 1);
  dataPreReqs.add_gathered_nodal_field(densityNm1_, 1);
  dataPreReqs.add_gathered_nodal_field(densityN_, 1);
  dataPreReqs.add_gathered_nodal_field(densityNp1_, 1);
  dataPreReqs.add_master_element_call(SCV_VOLUME, CURRENT_COORDINATES);

  diagRelaxFactor_ = solnOpts.get_relaxation_factor(scalarQ->name());
}

template<typename AlgTraits>
void
ScalarMassElemKernel<AlgTraits>::setup(const TimeIntegrator& timeIntegrator)
{
  dt_ = timeIntegrator.get_time_step();
  gamma1_ = timeIntegrator.get_gamma1();
  gamma2_ = timeIntegrator.get_gamma2();
  gamma3_ = timeIntegrator.get_gamma3(); // gamma3 may be zero
}

template<typename AlgTraits>
void
ScalarMassElemKernel<AlgTraits>::execute(
  SharedMemView<DoubleType **, DeviceShmem>& lhs,
  SharedMemView<DoubleType *, DeviceShmem>&rhs,
  ScratchViews<DoubleType, DeviceTeamHandleType, DeviceShmem>& scratchViews)
{
  auto& v_qNm1 = scratchViews.get_scratch_view_1D(scalarQNm1_);
  auto& v_qN = scratchViews.get_scratch_view_1D(scalarQN_);
  auto& v_qNp1 = scratchViews.get_scratch_view_1D(scalarQNp1_);
  auto& v_rhoNm1 = scratchViews.get_scratch_view_1D(densityNm1_);
  auto& v_rhoN = scratchViews.get_scratch_view_1D(densityN_);
  auto& v_rhoNp1 = scratchViews.get_scratch_view_1D(densityNp1_);
  auto& meViews = scratchViews.get_me_views(CURRENT_COORDINATES);
  auto& v_scv_volume = meViews.scv_volume;
  auto& v_shape_function = lumpedMass_ ? meViews.scv_shifted_shape_fcn : meViews.scv_shape_fcn;

  const int* ipNodeMap = meSCV_->ipNodeMap();

  for ( int ip = 0; ip < AlgTraits::numScvIp_; ++ip ) {

    // nearest node to ip
    const int nearestNode = ipNodeMap[ip];

    // zero out; scalar
    DoubleType qNm1Scv = 0.0;
    DoubleType qNScv = 0.0;
    DoubleType qNp1Scv = 0.0;
    DoubleType rhoNm1Scv = 0.0;
    DoubleType rhoNScv = 0.0;
    DoubleType rhoNp1Scv = 0.0;

    for ( int ic = 0; ic < AlgTraits::nodesPerElement_; ++ic ) {
      // save off shape function
      const DoubleType r = v_shape_function(ip,ic);

      // scalar q
      qNm1Scv += r*v_qNm1(ic);
      qNScv += r*v_qN(ic);
      qNp1Scv += r*v_qNp1(ic);

      // density
      rhoNm1Scv += r*v_rhoNm1(ic);
      rhoNScv += r*v_rhoN(ic);
      rhoNp1Scv += r*v_rhoNp1(ic);
    }

    // assemble rhs
    const DoubleType scV = v_scv_volume(ip);
    rhs(nearestNode) +=
      -(gamma1_*rhoNp1Scv*qNp1Scv + gamma2_*rhoNScv*qNScv + gamma3_*rhoNm1Scv*qNm1Scv)*scV/dt_;

    // manage LHS
    for ( int ic = 0; ic < AlgTraits::nodesPerElement_; ++ic ) {
      // save off shape function
      const DoubleType r = v_shape_function(ip,ic);
      const DoubleType lhsfac = r*gamma1_*rhoNp1Scv*scV/dt_ * diagRelaxFactor_;
      lhs(nearestNode,ic) += lhsfac;
    }
  }
}

INSTANTIATE_KERNEL(ScalarMassElemKernel)

}  // nalu
}  // sierra
