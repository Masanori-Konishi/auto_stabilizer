#include "FootStepGenerator.h"
#include "MathUtil.h"

bool FootStepGenerator::initFootStepNodesList(const cnoid::BodyPtr& genRobot, const GaitParam& gaitParam,
                                              std::vector<GaitParam::FootStepNodes>& o_footstepNodesList, std::vector<cnoid::Position>& o_srcCoords, std::vector<cnoid::Position>& o_dstCoordsOrg, std::vector<bool>& o_prevSupportPhase) const{
  // footStepNodesListを初期化する
  std::vector<GaitParam::FootStepNodes> footstepNodesList(1);
  std::vector<cnoid::Position> srcCoords;
  std::vector<cnoid::Position> dstCoordsOrg;
  cnoid::Position rlegCoords = genRobot->link(gaitParam.eeParentLink[RLEG])->T()*gaitParam.eeLocalT[RLEG];
  cnoid::Position llegCoords = genRobot->link(gaitParam.eeParentLink[LLEG])->T()*gaitParam.eeLocalT[LLEG];
  footstepNodesList[0].dstCoords = {rlegCoords, llegCoords};
  footstepNodesList[0].isSupportPhase = {true, true};
  footstepNodesList[0].remainTime = 0.0;
  srcCoords = footstepNodesList[0].dstCoords;
  dstCoordsOrg = footstepNodesList[0].dstCoords;

  std::vector<bool> prevSupportPhase(NUM_LEGS);
  for(int i=0;i<NUM_LEGS;i++){
    prevSupportPhase[i] = footstepNodesList[0].isSupportPhase[i];
  }

  o_prevSupportPhase = prevSupportPhase;
  o_footstepNodesList = footstepNodesList;
  o_srcCoords = srcCoords;
  o_dstCoordsOrg = dstCoordsOrg;

  return true;
}

bool FootStepGenerator::setFootSteps(const GaitParam& gaitParam, const std::vector<StepNode>& footsteps,
                                     std::vector<GaitParam::FootStepNodes>& o_footstepNodesList) const{
  if(footsteps.size() <= 1) { // 何もしない
    o_footstepNodesList = gaitParam.footstepNodesList;
    return true;
  }

  if(!gaitParam.isStatic()){ // 静止中でないと無効
    o_footstepNodesList = gaitParam.footstepNodesList;
    return false;
  }

  std::vector<GaitParam::FootStepNodes> footstepNodesList;
  footstepNodesList.push_back(gaitParam.footstepNodesList[0]);

  if(footstepNodesList.back().isSupportPhase[RLEG] && footstepNodesList.back().isSupportPhase[LLEG]){
    footstepNodesList.back().remainTime = this->defaultDoubleSupportTime;
  }else if(!footstepNodesList.back().isSupportPhase[RLEG] && footsteps[1].l_r == LLEG){ // RLEGを下ろす必要がある
    this->calcDefaultNextStep(footstepNodesList, gaitParam.defaultTranslatePos);
  }else if(!footstepNodesList.back().isSupportPhase[LLEG] && footsteps[1].l_r == RLEG){ // LLEGを下ろす必要がある
    this->calcDefaultNextStep(footstepNodesList, gaitParam.defaultTranslatePos);
  }

  // footstepsの0番目の要素は、実際には歩かず、基準座標としてのみ使われる. footstepNodesList[0].dstCoordsのZ軸を鉛直に直した座標系と、footstepsの0番目の要素のZ軸を鉛直に直した座標系が一致するように座標変換する.
  cnoid::Position trans;
  {
    cnoid::Position genCoords = mathutil::orientCoordToAxis(footstepNodesList.back().dstCoords[footsteps[0].l_r], cnoid::Vector3::UnitZ());
    cnoid::Position refCoords = mathutil::orientCoordToAxis(footsteps[0].coords, cnoid::Vector3::UnitZ());
    trans = genCoords * refCoords.inverse();
  }

  for(int i=1;i<footsteps.size();i++){
    GaitParam::FootStepNodes fs;
    int swingLeg = footsteps[i].l_r;
    int supportLeg = swingLeg == RLEG ? LLEG: RLEG;
    fs.dstCoords[supportLeg] = footstepNodesList.back().dstCoords[supportLeg];
    fs.dstCoords[swingLeg] = trans * footsteps[i].coords;
    fs.isSupportPhase[supportLeg] = true;
    fs.isSupportPhase[swingLeg] = false;
    if(footsteps[i].stepTime > this->defaultDoubleSupportTime){
      fs.remainTime = footsteps[i].stepTime - this->defaultDoubleSupportTime;
    }else{
      fs.remainTime = this->defaultStepTime - this->defaultDoubleSupportTime;
    }
    double stepHeight = std::max(0.0, footsteps[i].stepHeight);
    if(footstepNodesList.back().isSupportPhase[swingLeg]){
      fs.stepHeight[swingLeg] = {stepHeight,stepHeight};
    }else{
      fs.stepHeight[swingLeg] = {0.0,stepHeight};
    }
    footstepNodesList.push_back(fs);

    footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
  }

  o_footstepNodesList = footstepNodesList;
  return true;
}


bool FootStepGenerator::goStop(const GaitParam& gaitParam,
            std::vector<GaitParam::FootStepNodes>& o_footstepNodesList) const {
  if(gaitParam.isStatic()){
    o_footstepNodesList = gaitParam.footstepNodesList;
    return true;
  }

  std::vector<GaitParam::FootStepNodes> footstepNodesList = gaitParam.footstepNodesList;

  if(footstepNodesList.size()>2) footstepNodesList.erase(footstepNodesList.begin()+2,footstepNodesList.end());

  // 両脚が横に並ぶ位置に2歩歩く.
  for(int i=0;i<2;i++){
    this->calcDefaultNextStep(footstepNodesList, gaitParam.defaultTranslatePos);
  }

  o_footstepNodesList = footstepNodesList;
  return true;

}

bool FootStepGenerator::calcFootSteps(const GaitParam& gaitParam, const double& dt, bool useActState,
                                      std::vector<GaitParam::FootStepNodes>& o_footstepNodesList) const{
  std::vector<GaitParam::FootStepNodes> footstepNodesList = gaitParam.footstepNodesList;

  // goVelocityModeなら、進行方向に向けてfootStepNodesList[1] ~ footStepNodesList[goVelocityStepNum]の要素を機械的に計算してどんどん末尾appendしていく. cmdVelに応じてきまる
  if(this->isGoVelocityMode){
    if(gaitParam.prevSupportPhase[RLEG] != gaitParam.footstepNodesList[0].isSupportPhase[RLEG] ||
       gaitParam.prevSupportPhase[LLEG] != gaitParam.footstepNodesList[0].isSupportPhase[LLEG]){ // footstepの切り替わりのタイミング. this->updateGoVelocityStepsを毎周期呼ぶと、数値誤差でだんだん変にずれてくるので.
      this->updateGoVelocitySteps(footstepNodesList, gaitParam.defaultTranslatePos);
    }
    while(footstepNodesList.size() < this->goVelocityStepNum){
      this->calcDefaultNextStep(footstepNodesList, gaitParam.defaultTranslatePos, this->cmdVel * this->defaultStepTime);
    }
  }

  if(useActState && this->isModifyFootSteps && this->isEmergencyStepMode){
    // TODO
  }

  if(useActState && this->isModifyFootSteps){
    this->modifyFootSteps(footstepNodesList, gaitParam);
  }

  if(useActState){
    // 早づきしたらremainTimeをdtに減らしてすぐに次のnodeへ移る. この機能が無いと少しでもロボットが傾いて早づきするとジャンプするような挙動になる. 遅づきに備えるために、着地位置を下方にオフセットさせる
    //   remainTimeをdtに減らしてすぐに次のnodeへ移ろうとしているときに着地位置修正が入ると不安定になるので、modifyFootStepsよりも後にやる必要がある.
    this->checkEarlyTouchDown(footstepNodesList, gaitParam, dt);
  }

  o_footstepNodesList = footstepNodesList;
  return true;
}

bool FootStepGenerator::advanceFootStepNodesList(const GaitParam& gaitParam, double dt,
                                                 std::vector<GaitParam::FootStepNodes>& o_footstepNodesList, std::vector<cnoid::Position>& o_srcCoords, std::vector<cnoid::Position>& o_dstCoordsOrg, std::vector<bool>& o_prevSupportPhase) const{
  // prevSupportPhaseを記録
  std::vector<bool> prevSupportPhase(2);
  for(int i=0;i<NUM_LEGS;i++) prevSupportPhase[i] = gaitParam.footstepNodesList[0].isSupportPhase[i];

  // footstepNodesListを進める
  std::vector<GaitParam::FootStepNodes> footstepNodesList = gaitParam.footstepNodesList;
  std::vector<cnoid::Position> srcCoords = gaitParam.srcCoords;
  std::vector<cnoid::Position> dstCoordsOrg = gaitParam.dstCoordsOrg;
  footstepNodesList[0].remainTime = std::max(0.0, footstepNodesList[0].remainTime - dt);
  if(footstepNodesList[0].remainTime <= 0.0 && footstepNodesList.size() > 1){
    footstepNodesList.erase(footstepNodesList.begin()); // vectorではなくlistにするべき?
    for(int i=0;i<NUM_LEGS;i++) {
      srcCoords[i] = gaitParam.genCoords[i].value();
      dstCoordsOrg[i] = footstepNodesList[0].dstCoords[i];
    }
  }

  o_prevSupportPhase = prevSupportPhase;
  o_footstepNodesList = footstepNodesList;
  o_srcCoords = srcCoords;
  o_dstCoordsOrg = dstCoordsOrg;

  return true;
}

void FootStepGenerator::updateGoVelocitySteps(std::vector<GaitParam::FootStepNodes>& footstepNodesList, const std::vector<cnoid::Vector3>& defaultTranslatePos) const{
  for(int i=1;i<footstepNodesList.size()-1;i++){
    if(((footstepNodesList[i].isSupportPhase[RLEG] && !footstepNodesList[i].isSupportPhase[LLEG]) || (!footstepNodesList[i].isSupportPhase[RLEG] && footstepNodesList[i].isSupportPhase[LLEG])) && // 今が片脚支持期
       (footstepNodesList[i+1].isSupportPhase[RLEG] && footstepNodesList[i+1].isSupportPhase[LLEG])){ // 次が両足支持期
      int swingLeg = footstepNodesList[i].isSupportPhase[RLEG] ? LLEG : RLEG;
      int supportLeg = (swingLeg == RLEG) ? LLEG : RLEG;
      cnoid::Vector3 offset = this->cmdVel * (footstepNodesList[i].remainTime + footstepNodesList[i+1].remainTime);
      cnoid::Position transform = cnoid::Position::Identity(); // supportLeg相対(Z軸は鉛直)での次のswingLegの位置
      transform.linear() = cnoid::Matrix3(Eigen::AngleAxisd(mathutil::clamp(offset[2],this->defaultStrideLimitationTheta), cnoid::Vector3::UnitZ()));
      transform.translation() = - defaultTranslatePos[supportLeg] + cnoid::Vector3(offset[0], offset[1], 0.0) + transform.linear() * defaultTranslatePos[swingLeg];
      transform.translation() = mathutil::calcNearestPointOfHull(transform.translation(), this->defaultStrideLimitationHull[swingLeg]);
      cnoid::Position prevOrigin = mathutil::orientCoordToAxis(footstepNodesList[i-1].dstCoords[supportLeg], cnoid::Vector3::UnitZ());
      cnoid::Position dstCoords = prevOrigin * transform;

      cnoid::Position origin = mathutil::orientCoordToAxis(footstepNodesList[i].dstCoords[swingLeg], cnoid::Vector3::UnitZ());
      cnoid::Position displacement = origin.inverse() * dstCoords;
      this->transformFutureSteps(footstepNodesList, i, origin, displacement);
    }
  }
}

void FootStepGenerator::transformFutureSteps(std::vector<GaitParam::FootStepNodes>& footstepNodesList, int index, const cnoid::Position& transformOrigin/*generate frame*/, const cnoid::Position& transform/*transform Origin frame*/) const{
  cnoid::Position trans = transformOrigin * transform * transformOrigin.inverse();
  for(int l=0;l<NUM_LEGS;l++){
    bool swinged = false;
    for(int i=index;i<footstepNodesList.size();i++){
      if(!footstepNodesList[i].isSupportPhase[l]) swinged = true;
      if(swinged) footstepNodesList[i].dstCoords[l] = trans * footstepNodesList[i].dstCoords[l];
    }
  }
}

void FootStepGenerator::transformFutureSteps(std::vector<GaitParam::FootStepNodes>& footstepNodesList, int index, const cnoid::Vector3& transform/*generate frame*/) const{
  for(int l=0;l<NUM_LEGS;l++){
    bool swinged = false;
    for(int i=index;i<footstepNodesList.size();i++){
      if(!footstepNodesList[i].isSupportPhase[l]) swinged = true;
      if(swinged) footstepNodesList[i].dstCoords[l].translation() += transform;
    }
  }
}

// indexのsupportLegが次にswingするまでの間の位置を、generate frameでtransformだけ動かす
void FootStepGenerator::transformCurrentSupportSteps(int leg, std::vector<GaitParam::FootStepNodes>& footstepNodesList, const cnoid::Position& transform/*generate frame*/, int index) const{
  assert(0<=leg && leg < NUM_LEGS);
  for(int i=index;i<footstepNodesList.size();i++){
    if(!footstepNodesList[i].isSupportPhase[leg]) return;
    footstepNodesList[i].dstCoords[leg] = transform * footstepNodesList[i].dstCoords[leg];
  }
}

void FootStepGenerator::calcDefaultNextStep(std::vector<GaitParam::FootStepNodes>& footstepNodesList, const std::vector<cnoid::Vector3>& defaultTranslatePos, const cnoid::Vector3& offset) const{
  if(footstepNodesList.back().isSupportPhase[RLEG] && footstepNodesList.back().isSupportPhase[LLEG]){
    footstepNodesList.back().remainTime = std::max(footstepNodesList.back().remainTime, this->defaultDoubleSupportTime);
    if(footstepNodesList.size() == 1 ||
       (footstepNodesList[footstepNodesList.size()-2].isSupportPhase[RLEG] && footstepNodesList[footstepNodesList.size()-2].isSupportPhase[LLEG]) ||
       (!footstepNodesList[footstepNodesList.size()-2].isSupportPhase[RLEG] && !footstepNodesList[footstepNodesList.size()-2].isSupportPhase[LLEG]) ){
      // どっちをswingしてもいいので、進行方向に近いLegをswingする
      cnoid::Vector2 rlegTolleg = (defaultTranslatePos[LLEG] - defaultTranslatePos[RLEG]).head<2>();
      if(rlegTolleg.dot(offset.head<2>()) > 0) {
        footstepNodesList.push_back(calcDefaultSwingStep(LLEG, footstepNodesList.back(), defaultTranslatePos, offset)); // LLEGをswingする
        footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
      }else{
        footstepNodesList.push_back(calcDefaultSwingStep(RLEG, footstepNodesList.back(), defaultTranslatePos, offset)); // RLEGをswingする
        footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
      }
    }else if(footstepNodesList[footstepNodesList.size()-2].isSupportPhase[RLEG]){ // 前回LLEGをswingした
        footstepNodesList.push_back(calcDefaultSwingStep(RLEG, footstepNodesList.back(), defaultTranslatePos, offset)); // RLEGをswingする
        footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
    }else{ // 前回RLEGをswingした
        footstepNodesList.push_back(calcDefaultSwingStep(LLEG, footstepNodesList.back(), defaultTranslatePos, offset)); // LLEGをswingする
        footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
    }
  }else if(footstepNodesList.back().isSupportPhase[RLEG] && !footstepNodesList.back().isSupportPhase[LLEG]){ // LLEGが浮いている
    footstepNodesList.push_back(calcDefaultSwingStep(LLEG, footstepNodesList.back(), defaultTranslatePos, offset, true)); // LLEGをswingする. startWithSingleSupport
    footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
  }else if(!footstepNodesList.back().isSupportPhase[RLEG] && footstepNodesList.back().isSupportPhase[LLEG]){ // RLEGが浮いている
    footstepNodesList.push_back(calcDefaultSwingStep(RLEG, footstepNodesList.back(), defaultTranslatePos, offset, true)); // RLEGをswingする. startWithSingleSupport
    footstepNodesList.push_back(calcDefaultDoubleSupportStep(footstepNodesList.back()));
  }// footstepNodesListの末尾の要素が両方falseであることは無い
}

GaitParam::FootStepNodes FootStepGenerator::calcDefaultSwingStep(const int& swingLeg, const GaitParam::FootStepNodes& footstepNodes, const std::vector<cnoid::Vector3>& defaultTranslatePos, const cnoid::Vector3& offset, bool startWithSingleSupport) const{
  GaitParam::FootStepNodes fs;
  int supportLeg = (swingLeg == RLEG) ? LLEG : RLEG;

  cnoid::Position transform = cnoid::Position::Identity(); // supportLeg相対(Z軸は鉛直)での次のswingLegの位置
  transform.linear() = cnoid::Matrix3(Eigen::AngleAxisd(mathutil::clamp(offset[2],this->defaultStrideLimitationTheta), cnoid::Vector3::UnitZ()));
  transform.translation() = - defaultTranslatePos[supportLeg] + cnoid::Vector3(offset[0], offset[1], 0.0) + transform.linear() * defaultTranslatePos[swingLeg];
  transform.translation() = mathutil::calcNearestPointOfHull(transform.translation(), this->defaultStrideLimitationHull[swingLeg]);

  fs.dstCoords[supportLeg] = footstepNodes.dstCoords[supportLeg];
  cnoid::Position prevOrigin = mathutil::orientCoordToAxis(footstepNodes.dstCoords[supportLeg], cnoid::Vector3::UnitZ());
  fs.dstCoords[swingLeg] = prevOrigin * transform;
  fs.isSupportPhase[supportLeg] = true;
  fs.isSupportPhase[swingLeg] = false;
  fs.remainTime = this->defaultStepTime - this->defaultDoubleSupportTime;
  if(!startWithSingleSupport) fs.stepHeight[swingLeg] = {this->defaultStepHeight,this->defaultStepHeight};
  else fs.stepHeight[swingLeg] = {0.0,this->defaultStepHeight};
  return fs;
}

GaitParam::FootStepNodes FootStepGenerator::calcDefaultDoubleSupportStep(const GaitParam::FootStepNodes& footstepNodes) const{
  GaitParam::FootStepNodes fs;
  for(int i=0;i<NUM_LEGS;i++){
    fs.dstCoords[i] = footstepNodes.dstCoords[i];
    fs.isSupportPhase[i] = true;
    fs.stepHeight[i] = {0.0,0.0};
  }
  fs.remainTime = this->defaultDoubleSupportTime;
  return fs;
}

inline std::ostream &operator<<(std::ostream &os, const std::vector<std::pair<std::vector<cnoid::Vector3>, double> >& candidates){
  for(int i=0;i<candidates.size();i++){
    os << "candidates[" << i << "] " << candidates[i].second << "s" << std::endl;
    for(int j=0;j<candidates[i].first.size();j++){
      os << candidates[i].first[j].format(Eigen::IOFormat(Eigen::StreamPrecision, 0, ", ", ", ", "", "", " [", "]"));
    }
    os << std::endl;
  }
  return os;
}

void FootStepGenerator::modifyFootSteps(std::vector<GaitParam::FootStepNodes>& footstepNodesList, const GaitParam& gaitParam) const{
  // 現在片足支持期で、次が両足支持期であるときのみ、行う
  if(!(footstepNodesList.size() > 1 &&
       (footstepNodesList[1].isSupportPhase[RLEG] && footstepNodesList[1].isSupportPhase[LLEG]) &&
       (footstepNodesList[0].isSupportPhase[RLEG] && !footstepNodesList[0].isSupportPhase[LLEG]) || (!footstepNodesList[0].isSupportPhase[RLEG] && footstepNodesList[0].isSupportPhase[LLEG])))
     return;

  // one step capturabilityに基づき、footstepNodesList[0]のremainTimeとdstCoordsを修正する.
  int swingLeg = footstepNodesList[0].isSupportPhase[RLEG] ? LLEG : RLEG;
  int supportLeg = (swingLeg == RLEG) ? LLEG : RLEG;
  cnoid::Position swingPose = gaitParam.genCoords[swingLeg].value();
  cnoid::Position supportPose = gaitParam.genCoords[supportLeg].value(); // TODO. 支持脚のgenCoordsとdstCoordsが異なることは想定していない
  cnoid::Position supportPoseHorizontal = mathutil::orientCoordToAxis(supportPose, cnoid::Vector3::UnitZ());

  // dx = w ( x - z - l)
  cnoid::Vector3 actDCM = gaitParam.actCog + gaitParam.actCogVel.value() / gaitParam.omega;

  /*
    capturable: ある時刻t(overwritableMinTime<=t<=overwritableMaxTime)が存在し、時刻tに着地すれば転倒しないような着地位置.
    reachable: ある時刻t(overwritableMinTime<=t<=overwritableMaxTime)が存在し、今の脚の位置からの距離が時刻tに着地することができる範囲である着地位置.
    strideLimitation: overwritableStrideLimitationHullの範囲内の着地位置(自己干渉・IKの考慮が含まれる).
    steppable: 着地可能な地形であるような着地位置

    * capturableとreachableの積集合を考えるときは、各時刻のtごとで考える

    優先度(小さいほうが高い)
    1. strideLimitation: 絶対満たす
    1. reachable: 絶対満たす
    2. steppable: 達成不可の場合は、考慮しない
    3. capturable: 達成不可の場合は、可能な限り近い位置. 複数ある場合は時間が速い方優先. (次の一歩に期待) (角運動量 TODO)
    4. もとの着地位置(dstCoordsOrg): 達成不可の場合は、各hullの中の最も近い位置をそれぞれ求めて、着地位置修正前の進行方向(遊脚のsrcCoordsからの方向)に最も進むもの優先 (支持脚からの方向にすると、横歩き時に後ろ足の方向が逆になってしまう)
    5. もとの着地時刻(remainTime): 達成不可の場合は、可能な限り近い時刻
   */

  std::vector<std::pair<std::vector<cnoid::Vector3>, double> > candidates; // first: generate frame. 着地領域(convex Hull). second: 着地時刻. サイズが0になることはない

  // 1. strideLimitation と reachable
  {
    std::vector<double> samplingTimes;
    samplingTimes.push_back(footstepNodesList[0].remainTime);
    int sample = 10;
    for(int i=0;i<=sample;i++) {
      double minTime = std::min(this->overwritableMinTime, footstepNodesList[0].remainTime); // 次indexまでの残り時間がthis->overwritableMinTimeを下回るようには着地時間修正を行わない. もともと下回っている場合には、その値を下回るようには着地時刻修正を行わない.
      double t = minTime + (this->overwritableMaxTime - minTime) / sample * i; // overwritableMaxTimeを上回ったりするようには着地時刻修正を行わない
      if(t != footstepNodesList[0].remainTime) samplingTimes.push_back(t);
    }

    std::vector<cnoid::Vector3> strideLimitationHull; // generate frame. overwritableStrideLimitationHullの範囲内の着地位置(自己干渉・IKの考慮が含まれる). Z成分には0を入れる
    for(int i=0;i<this->overwritableStrideLimitationHull[swingLeg].size();i++){
      cnoid::Vector3 p = supportPoseHorizontal * this->overwritableStrideLimitationHull[swingLeg][i];
      strideLimitationHull.emplace_back(p[0],p[1],0.0);
    }

    for(int i=0;i<samplingTimes.size();i++){
      double t = samplingTimes[i];
      std::vector<cnoid::Vector3> reachableHull; // generate frame. 今の脚の位置からの距離が時刻tに着地することができる範囲. Z成分には0を入れる
      int segment = 8;
      for(int j=0; j < segment; j++){
        reachableHull.emplace_back(swingPose.translation()[0] + this->overwritableMaxSwingVelocity * t * std::cos(2 * M_PI / segment * j),
                                   swingPose.translation()[1] + this->overwritableMaxSwingVelocity * t * std::sin(2 * M_PI / segment * j),
                                   0.0);
      }
      std::vector<cnoid::Vector3> hull = mathutil::calcIntersectConvexHull(reachableHull, strideLimitationHull);
      if(hull.size() > 0) candidates.emplace_back(hull, t);
    }

    if(candidates.size() == 0) candidates.emplace_back(std::vector<cnoid::Vector3>{footstepNodesList[0].dstCoords[swingLeg].translation()}, footstepNodesList[0].remainTime); // まず起こらないと思うが念の為
  }
  // std::cerr << "strideLimitation と reachable" << std::endl;
  // std::cerr << candidates << std::endl;

  // 2. steppable: 達成不可の場合は、考慮しない
  // TODO. Z高さの扱い.(DOWN_PHASEのときはfootstepNodesList[0]のdstCoordsはgenCoordsよりも高い位置に変更されることはない) (高低差と時間の関係)

  // 3. capturable: 達成不可の場合は、可能な限り近い位置. 複数ある場合は時間が速い方優先. (次の一歩に期待) (角運動量 TODO)
  // 次の両足支持期終了時に入るケースでもOKにしたい
  {
    std::vector<std::vector<cnoid::Vector3> > capturableHulls; // 要素数と順番はcandidatesに対応
    for(int i=0;i<candidates.size();i++){
      std::vector<cnoid::Vector3> capturableVetices; // generate frame. 時刻tに着地すれば転倒しないような着地位置. Z成分には0を入れる
      for(double t = candidates[i].second; t <= candidates[i].second + footstepNodesList[1].remainTime; t += footstepNodesList[1].remainTime){ // 接地する瞬間と、次の両足支持期の終了時. 片方だけだと特に横歩きのときに厳しすぎる.
        for(int j=0;j<this->safeLegHull[supportLeg].size();j++){
          cnoid::Vector3 zmp = supportPose * this->safeLegHull[supportLeg][j];// generate frame
          cnoid::Vector3 endDCM = (actDCM - zmp - gaitParam.l) * std::exp(gaitParam.omega * t) + zmp + gaitParam.l; // generate frame. 着地時のDCM
          // for(int k=0;k<this->safeLegHull[swingLeg].size();k++){
          //   cnoid::Vector3 p = endDCM - gaitParam.l - footstepNodesList[0].dstCoords[swingLeg].linear() * this->safeLegHull[swingLeg][k]; // こっちのほうが厳密であり、着地位置時刻修正を最小限にできるが、ロバストさに欠ける
          //   capturableVetices.emplace_back(p[0], p[1], 0.0);
          // }
          cnoid::Vector3 p = endDCM - gaitParam.l - footstepNodesList[0].dstCoords[swingLeg].linear() * gaitParam.copOffset[swingLeg];
          capturableVetices.emplace_back(p[0], p[1], 0.0);
        }
      }
      capturableHulls.push_back(mathutil::calcConvexHull(capturableVetices)); // generate frame. 時刻tに着地すれば転倒しないような着地位置. Z成分には0を入れる
    }

    std::vector<std::pair<std::vector<cnoid::Vector3>, double> > nextCandidates;
    for(int i=0;i<candidates.size();i++){
      std::vector<cnoid::Vector3> hull = mathutil::calcIntersectConvexHull(candidates[i].first, capturableHulls[i]);
      if(hull.size() > 0) nextCandidates.emplace_back(hull, candidates[i].second);
    }
    if(nextCandidates.size() > 0) candidates = nextCandidates;
    else{
      // 達成不可の場合は、時間が速い方優先(次の一歩に期待). 複数ある場合は可能な限り近い位置.
      //   どうせこの一歩ではバランスがとれないので、位置よりも速く次の一歩に移ることを優先したほうが良い
      double minTime = std::numeric_limits<double>::max();
      double minDistance = std::numeric_limits<double>::max();
      cnoid::Vector3 minp;
      for(int i=0;i<candidates.size();i++){
        if(candidates[i].second <= minTime){
          std::vector<cnoid::Vector3> p, q;
          double distance = mathutil::calcNearestPointOfTwoHull(candidates[i].first, capturableHulls[i], p, q); // candidates[i].first, capturableHulls[i]は重なっていない・接していない
          if(candidates[i].second < minTime ||
             (candidates[i].second == minTime && distance < minDistance)){
            minTime = candidates[i].second;
            minDistance = distance;
            nextCandidates.clear();
            nextCandidates.emplace_back(p,minTime); // pは、最近傍が点の場合はその点が入っていて、最近傍が線分の場合はその線分の両端点が入っている
          }else if(candidates[i].second == minTime && distance == minDistance){
            nextCandidates.emplace_back(p,minTime);
          }
        }
      }
      candidates = nextCandidates;
    }
  }

  // std::cerr << "capturable" << std::endl;
  // std::cerr << candidates << std::endl;

  // 4. もとの着地位置: 達成不可の場合は、各hullの中の最も近い位置をそれぞれ求めて、着地位置修正前の進行方向(遊脚のsrcCoordsからの方向)に最も進むもの優先 (支持脚からの方向にすると、横歩き時に後ろ足の方向が逆になってしまう)
  {
    std::vector<std::pair<std::vector<cnoid::Vector3>, double> > nextCandidates;
    for(int i=0;i<candidates.size();i++){
      if(mathutil::isInsideHull(gaitParam.dstCoordsOrg[swingLeg].translation(), candidates[i].first)){
        nextCandidates.emplace_back(std::vector<cnoid::Vector3>{gaitParam.dstCoordsOrg[swingLeg].translation()},candidates[i].second);
      }
    }
    if(nextCandidates.size() > 0) candidates = nextCandidates;
    else{ // 達成不可の場合は、各hullの中の最も近い位置をそれぞれ求めて、着地位置修正前の進行方向(遊脚のsrcCoordsからの方向)に最も進むもの優先
      cnoid::Vector3 dir = gaitParam.dstCoordsOrg[swingLeg].translation() - gaitParam.srcCoords[swingLeg].translation(); dir[2] = 0.0;
      if(dir.norm() != 0){ //各hullの中の最も近い位置をそれぞれ求めて、着地位置修正前の進行方向(遊脚のsrcCoordsからの方向)に最も進むもの優先
        dir = dir.normalized();
        double maxVel = - std::numeric_limits<double>::max();
        for(int i=0;i<candidates.size();i++){
          cnoid::Vector3 p = mathutil::calcNearestPointOfHull(gaitParam.dstCoordsOrg[swingLeg].translation(), candidates[i].first);
          double vel = (p - gaitParam.srcCoords[swingLeg].translation()).dot(dir);
          if(vel > maxVel){
            maxVel = vel;
            nextCandidates.clear();
            nextCandidates.emplace_back(std::vector<cnoid::Vector3>{p}, candidates[i].second);
          }else if (vel == maxVel){
            maxVel = vel;
            nextCandidates.emplace_back(std::vector<cnoid::Vector3>{p}, candidates[i].second);
          }
        }
      }else{ // 進行方向が定義できない. //各hullの中の最も近い位置をそれぞれ求めて、遊脚のsrcCoordsからの距離が最も小さいもの優先
        double minVel = + std::numeric_limits<double>::max();
        for(int i=0;i<candidates.size();i++){
          cnoid::Vector3 p = mathutil::calcNearestPointOfHull(gaitParam.dstCoordsOrg[swingLeg].translation(), candidates[i].first);
          double vel = (p - gaitParam.srcCoords[swingLeg].translation()).norm();
          if(vel < minVel){
            minVel = vel;
            nextCandidates.clear();
            nextCandidates.emplace_back(std::vector<cnoid::Vector3>{p}, candidates[i].second);
          }else if (vel == minVel){
            minVel = vel;
            nextCandidates.emplace_back(std::vector<cnoid::Vector3>{p}, candidates[i].second);
          }
        }
      }
      candidates = nextCandidates;
    }
  }

  // std::cerr << "pos" << std::endl;
  // std::cerr << candidates << std::endl;

  // 5. もとの着地時刻(remainTime): 達成不可の場合は、可能な限り近い時刻
  {
    std::vector<std::pair<std::vector<cnoid::Vector3>, double> > nextCandidates;
    double minDiffTime = std::numeric_limits<double>::max();
    for(int i=0;i<candidates.size();i++){
      double diffTime = std::abs(candidates[i].second - footstepNodesList[0].remainTime);
      if(diffTime < minDiffTime){
        minDiffTime = diffTime;
        nextCandidates.clear();
        nextCandidates.push_back(candidates[i]);
      }else if(diffTime == minDiffTime){
        minDiffTime = diffTime;
        nextCandidates.push_back(candidates[i]);
      }
    }
    candidates = nextCandidates;
  }

  // std::cerr << "time" << std::endl;
  // std::cerr << candidates << std::endl;

  // 修正を適用
  cnoid::Vector3 nextDstCoordsPos = candidates[0].first[0];
  cnoid::Vector3 displacement = nextDstCoordsPos - footstepNodesList[0].dstCoords[swingLeg].translation();
  displacement[2] = 0.0;
  this->transformFutureSteps(footstepNodesList, 0, displacement);
  footstepNodesList[0].remainTime = candidates[0].second;
}

// 早づきしたらremainTimeをdtに減らしてすぐに次のnodeへ移る. この機能が無いと少しでもロボットが傾いて早づきするとジャンプするような挙動になる. 遅づきに備えるために、着地位置を下方にオフセットさせる
void FootStepGenerator::checkEarlyTouchDown(std::vector<GaitParam::FootStepNodes>& footstepNodesList, const GaitParam& gaitParam, double dt) const{
  for(int i=0;i<NUM_LEGS;i++){
    actLegWrenchFilter[i].passFilter(gaitParam.actEEWrench[i], dt);
  }

  // 現在片足支持期で、次が両足支持期であるときのみ、行う
  if(!(footstepNodesList.size() > 1 &&
       (footstepNodesList[1].isSupportPhase[RLEG] && footstepNodesList[1].isSupportPhase[LLEG]) &&
       (footstepNodesList[0].isSupportPhase[RLEG] && !footstepNodesList[0].isSupportPhase[LLEG]) || (!footstepNodesList[0].isSupportPhase[RLEG] && footstepNodesList[0].isSupportPhase[LLEG])))
     return;

  int swingLeg = footstepNodesList[0].isSupportPhase[RLEG] ? LLEG : RLEG;
  int supportLeg = (swingLeg == RLEG) ? LLEG : RLEG;

  // DOWN_PHASEのときのみ行う
  if(footstepNodesList[0].swingState[swingLeg] != GaitParam::FootStepNodes::DOWN_PHASE) return;

  if(actLegWrenchFilter[swingLeg].value()[2] > this->contactDecisionThreshold /*generate frame. ロボットが受ける力*/ ||
     footstepNodesList[0].remainTime <= dt // 地面につかないままswingphase終了
     ){
    {
      cnoid::Position origin = mathutil::orientCoordToAxis(footstepNodesList[0].dstCoords[swingLeg], cnoid::Vector3::UnitZ()); // generate frame
      cnoid::Position transform = origin.inverse() * mathutil::orientCoordToAxis(gaitParam.genCoords[swingLeg].value(), cnoid::Vector3::UnitZ()); // footstepNodesList[0].dstCoords[swingLeg] frame
      this->transformFutureSteps(footstepNodesList, 0, origin, transform); // 遊脚を今の位置姿勢(Z軸は鉛直)でとめ、連動して将来の着地位置も変える
    }
    {
      cnoid::Position diff = gaitParam.genCoords[swingLeg].value() * footstepNodesList[0].dstCoords[swingLeg].inverse();
      footstepNodesList[0].dstCoords[swingLeg] = gaitParam.genCoords[swingLeg].value(); // 遊脚を今の傾きでとめる
      this->transformCurrentSupportSteps(swingLeg, footstepNodesList, diff, 1); // 遊脚の次の支持脚期間を傾きを今の傾きにする
    }
    {
      cnoid::Position diff = gaitParam.genCoords[supportLeg].value() * footstepNodesList[0].dstCoords[supportLeg].inverse();
      this->transformCurrentSupportSteps(supportLeg, footstepNodesList, diff, 0); // 支持脚を今の位置姿勢で止める
    }
    footstepNodesList[0].remainTime = dt;
    footstepNodesList[0].goalOffset[swingLeg] = 0.0;

  }else{
    footstepNodesList[0].goalOffset[swingLeg] = this->goalOffset;
  }
}
