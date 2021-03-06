// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//


#ifndef MOMENTUMMASSELEMKERNEL_H
#define MOMENTUMMASSELEMKERNEL_H

#include "kernel/Kernel.h"
#include "FieldTypeDef.h"

#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Entity.hpp>

#include <Kokkos_Core.hpp>

namespace sierra {
namespace nalu {

class TimeIntegrator;
class SolutionOptions;
class MasterElement;
class ElemDataRequests;

/** CMM (BDF2/BE) for momentum equation (velocity DOF)
 */
template<typename AlgTraits>
class MomentumMassElemKernel: public NGPKernel<MomentumMassElemKernel<AlgTraits>>
{
public:
  MomentumMassElemKernel(
    const stk::mesh::BulkData&,
    const SolutionOptions&,
    ElemDataRequests&,
    const bool);

  KOKKOS_DEFAULTED_FUNCTION MomentumMassElemKernel() = default;

  virtual ~MomentumMassElemKernel() = default;

  /** Perform pre-timestep work for the computational kernel
   */
  virtual void setup(const TimeIntegrator&);

  /** Execute the kernel within a Kokkos loop and populate the LHS and RHS for
   *  the linear solve
   */
  using Kernel::execute;

  KOKKOS_FUNCTION
  virtual void execute(
    SharedMemView<DoubleType**, DeviceShmem>&,
    SharedMemView<DoubleType*, DeviceShmem>&,
    ScratchViews<DoubleType, DeviceTeamHandleType, DeviceShmem>&);

private:
  unsigned velocityNm1_ {stk::mesh::InvalidOrdinal};
  unsigned velocityN_   {stk::mesh::InvalidOrdinal};
  unsigned velocityNp1_ {stk::mesh::InvalidOrdinal};
  unsigned densityNm1_  {stk::mesh::InvalidOrdinal};
  unsigned densityN_    {stk::mesh::InvalidOrdinal};
  unsigned densityNp1_  {stk::mesh::InvalidOrdinal};
  unsigned Gjp_         {stk::mesh::InvalidOrdinal};
  unsigned coordinates_ {stk::mesh::InvalidOrdinal};

  double dt_{0.0};
  double gamma1_{0.0};
  double gamma2_{0.0};
  double gamma3_{0.0};
  double diagRelaxFactor_{1.0};
  const bool lumpedMass_;

  MasterElement* meSCV_{nullptr};
};

}  // nalu
}  // sierra

#endif /* MOMENTUMMASSELEMKERNEL_H */
