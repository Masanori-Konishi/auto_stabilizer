#include "RefToGenFrameConverter.h"
#include "CnoidBodyUtil.h"
#include "MathUtil.h"

bool RefToGenFrameConverter::initGenRobot(const cnoid::BodyPtr& refRobot, const EndEffectorParam& endEffectorParams, const GaitParam& gaitParam, // input
                                         cnoid::BodyPtr& genRobot, cpp_filters::TwoPointInterpolatorSE3& o_footMidCoords, cnoid::Vector3& o_genCog, cnoid::Vector3& o_genCogVel) const{ // output
  genRobot->rootLink()->T() = refRobot->rootLink()->T();
  genRobot->rootLink()->v() = refRobot->rootLink()->v();
  genRobot->rootLink()->w() = refRobot->rootLink()->w();
  for(int i=0;i<genRobot->numJoints();i++){
    genRobot->joint(i)->q() = refRobot->joint(i)->q();
    genRobot->joint(i)->dq() = refRobot->joint(i)->dq();
    genRobot->joint(i)->u() = refRobot->joint(i)->u();
  }
  genRobot->calcForwardKinematics();
  cnoid::Position refFootMidCoords = this->calcRefFootMidCoords(genRobot, endEffectorParams, gaitParam);
  cnoid::Position footMidCoords = mathutil::orientCoordToAxis(refFootMidCoords, cnoid::Vector3::UnitZ());
  cnoidbodyutil::moveCoords(genRobot, footMidCoords, refFootMidCoords);
  genRobot->calcForwardKinematics(true);
  genRobot->calcCenterOfMass();

  cnoid::Vector3 genCog = genRobot->centerOfMass();
  cnoid::Vector3 genCogVel = cnoid::Vector3::Zero();

  o_footMidCoords.reset(footMidCoords);
  o_genCog = genCog;
  o_genCogVel = genCogVel;
  return true;
}

bool RefToGenFrameConverter::convertFrame(const cnoid::BodyPtr& refRobot, const EndEffectorParam& endEffectorParams, const GaitParam& gaitParam, // input
                                         cnoid::BodyPtr& refRobotOrigin, std::vector<cnoid::Position>& o_refPose, std::vector<cnoid::Vector6>& o_refWrench, double& o_dz) const{ // output
  // refRobotOriginを計算
  for(int i=0;i<refRobotOrigin->numJoints();i++){
    refRobotOrigin->joint(i)->q() = refRobot->joint(i)->q();
    refRobotOrigin->joint(i)->dq() = refRobot->joint(i)->dq();
    refRobotOrigin->joint(i)->u() = refRobot->joint(i)->u();
  }
  refRobotOrigin->calcForwardKinematics();
  cnoid::Position refFootMidCoords = this->calcRefFootMidCoords(refRobotOrigin, endEffectorParams, gaitParam);
  cnoidbodyutil::moveCoords(refRobotOrigin, gaitParam.footMidCoords.value(), refFootMidCoords); // 1周期前のfootMidCoordsを使っているが、footMidCoordsは不連続に変化するものではないのでよい
  refRobotOrigin->calcForwardKinematics();
  refRobotOrigin->calcCenterOfMass();

  // refPoseを計算
  std::vector<cnoid::Position> refPose(endEffectorParams.name.size());
  for(int i=0;i<endEffectorParams.name.size();i++){
    refPose[i] = refRobotOrigin->link(endEffectorParams.parentLink[i])->T() * endEffectorParams.localT[i];
  }

  // refWrenchを計算
  std::vector<cnoid::Vector6> refWrench(endEffectorParams.name.size());
  for(int i=0;i<endEffectorParams.name.size();i++){
    refWrench[i].head<3>() = gaitParam.footMidCoords.value().linear() * endEffectorParams.refWrenchOrigin[i].head<3>();
    refWrench[i].tail<3>() = gaitParam.footMidCoords.value().linear() * endEffectorParams.refWrenchOrigin[i].tail<3>();
  }

  // ref重心高さ
  double dz = refRobotOrigin->centerOfMass()[2] - gaitParam.footMidCoords.value().translation()[2];

  o_refPose = refPose;
  o_refWrench = refWrench;
  o_dz = dz;

  return true;
}

cnoid::Position RefToGenFrameConverter::calcRefFootMidCoords(const cnoid::BodyPtr& robot, const EndEffectorParam& endEffectorParams, const GaitParam& gaitParam) const {

  cnoid::Position rleg = robot->link(endEffectorParams.parentLink[RLEG])->T()*endEffectorParams.localT[RLEG];
  rleg.translation() += rleg.linear() * gaitParam.copOffset[RLEG];
  cnoid::Position lleg = robot->link(endEffectorParams.parentLink[LLEG])->T()*endEffectorParams.localT[LLEG];
  lleg.translation() += lleg.linear() * gaitParam.copOffset[LLEG];

  cnoid::Position bothmidcoords = mathutil::calcMidCoords(std::vector<cnoid::Position>{rleg, lleg},
                                                          std::vector<double>{1.0, 1.0});
  cnoid::Position rlegmidcoords = rleg; rlegmidcoords.translation() -= rlegmidcoords.linear() * gaitParam.defaultTranslatePos[RLEG];
  cnoid::Position llegmidcoords = lleg; llegmidcoords.translation() -= llegmidcoords.linear() * gaitParam.defaultTranslatePos[LLEG];

  double bothweight = std::min(this->refFootOriginWeight[RLEG].value(), this->refFootOriginWeight[LLEG].value());
  double rlegweight = this->refFootOriginWeight[RLEG].value() - bothweight;
  double llegweight = this->refFootOriginWeight[LLEG].value() - bothweight;
  return mathutil::calcMidCoords(std::vector<cnoid::Position>{bothmidcoords, rlegmidcoords, llegmidcoords},
                                 std::vector<double>{bothweight, rlegweight, llegweight});
}