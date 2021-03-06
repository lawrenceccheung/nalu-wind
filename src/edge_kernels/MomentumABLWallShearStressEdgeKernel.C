// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//


#include "edge_kernels/MomentumABLWallShearStressEdgeKernel.h"
#include "master_element/MasterElement.h"
#include "master_element/MasterElementFactory.h"
#include "SolutionOptions.h"

#include "BuildTemplates.h"
#include "ScratchViews.h"
#include "utils/StkHelpers.h"
#include "wind_energy/MoninObukhov.h"

#include "stk_mesh/base/Field.hpp"

namespace sierra {
namespace nalu {

template<typename BcAlgTraits>
MomentumABLWallShearStressEdgeKernel<BcAlgTraits>::MomentumABLWallShearStressEdgeKernel(
  stk::mesh::MetaData& meta,
  ElemDataRequests& faceDataPreReqs
) : NGPKernel<MomentumABLWallShearStressEdgeKernel<BcAlgTraits>>(),
    exposedAreaVec_(get_field_ordinal(meta, "exposed_area_vector", meta.side_rank())),
    wallShearStress_(get_field_ordinal(meta, "wall_shear_stress_bip", meta.side_rank())),
    meFC_(sierra::nalu::MasterElementRepo::get_surface_master_element<BcAlgTraits>())
{
  faceDataPreReqs.add_cvfem_face_me(meFC_);

  faceDataPreReqs.add_face_field(exposedAreaVec_, BcAlgTraits::numFaceIp_, BcAlgTraits::nDim_);
  faceDataPreReqs.add_face_field(wallShearStress_, BcAlgTraits::numFaceIp_, BcAlgTraits::nDim_);
}

template<typename BcAlgTraits>
void
MomentumABLWallShearStressEdgeKernel<BcAlgTraits>::execute(
  SharedMemView<DoubleType**, DeviceShmem>& /* lhs */,
  SharedMemView<DoubleType*, DeviceShmem>& rhs,
  ScratchViews<DoubleType, DeviceTeamHandleType, DeviceShmem>& scratchViews)
{

  NALU_ALIGNED DoubleType tauWall[BcAlgTraits::nDim_];

  const auto& v_areavec = scratchViews.get_scratch_view_2D(exposedAreaVec_);
  const auto& v_wallshearstress = scratchViews.get_scratch_view_2D(wallShearStress_);

  const int* ipNodeMap = meFC_->ipNodeMap();

  for (int ip = 0; ip < BcAlgTraits::numFaceIp_; ++ip) {
    const int nodeR = ipNodeMap[ip];

    DoubleType amag = 0.0;
    for (int d=0; d < BcAlgTraits::nDim_; ++d) {
      tauWall[d] = v_wallshearstress(ip, d);
      amag += v_areavec(ip, d) * v_areavec(ip, d);
    }
    amag = stk::math::sqrt(amag);

    for (int i=0; i < BcAlgTraits::nDim_; ++i) {
      const int rowR = nodeR * BcAlgTraits::nDim_ + i;
      rhs(rowR) += tauWall[i]*amag;
    }
  }
}

INSTANTIATE_KERNEL_FACE(MomentumABLWallShearStressEdgeKernel)

}  // nalu
}  // sierra
