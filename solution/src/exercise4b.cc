// Exercise4-adv.: Navier-Stokes equations (advanced)
//
// Goal: implement a Newton-solver for Navier-Stokes
//
// Reference: see https://goo.gl/JK8EUI for a detailed description of possible 
//            expression terms.
//
// Compile-and-run: 
// > cd build
// > make exercise4b
// > cd ..
// > build/exercise4b init/exercise4b.dat.2d
//

#include "AMDiS.h"

using namespace AMDiS;

struct G : AbstractFunction<double, WorldVector<double> >
{
  G(int comp, double vel = 1.0) 
    : comp(comp), vel(vel) 
  {}
  
  double operator()(WorldVector<double> const& x) const 
  {
    return comp == 0 ? vel*x[1]*(1.0 - 0.25*x[1]) : 0.0;
  }
private:
  int comp;
  double vel;
};

struct NavierStokes : ProblemInstat
{
  NavierStokes(std::string name, 
		  ProblemStatSeq& prob,
		  ProblemStatBase& initialProb)
    : ProblemInstat(name, prob, initialProb) {}
    
  /// Used by \ref problemInitial
  virtual void transferInitialSolution(AdaptInfo *adaptInfo)
  {
    ProblemInstat::transferInitialSolution(adaptInfo);
    *problemStat->getSolution() = *dynamic_cast<ProblemStat*>(initialProblem)->getSolution();
  }
};

// solve: dt(u) - laplace(u) = f(x) in Omega,    u = 0 on Gamma
int main(int argc, char* argv[])
{
  AMDiS::init(argc, argv);

  // ===== create and init the scalar problem ===== 
  ProblemNonLin prob("ns");
  prob.initialize(INIT_ALL);
  
  ProblemStat initial_prob("ns");
  initial_prob.initialize(INIT_ALL - INIT_MESH - INIT_FE_SPACE, &prob, INIT_MESH | INIT_FE_SPACE);
  
  NavierStokes instat("ns", prob, initial_prob);
  instat.initialize(INIT_ALL);

  // ===== create info-object, that holds parameters ===
  AdaptInfo adaptInfo("adapt", prob.getNumComponents());
  
  int dow = Global::getGeo(WORLD);
  double* tau = adaptInfo.getTimestepPtr();
  double nu = 1.0, vel = 1.0;
  Parameters::get("parameters->nu", nu);
  Parameters::get("parameters->vel", vel);
  
  // shortcuts for solution vector
  WorldVector<DOFVector<double>*> u, u_old;
  for (int i = 0; i < dow; ++i) {
    u[i] = prob.getSolution(i);
    u_old[i] = instat.getOldSolution(i);
  }
  
  // INITIAL_PROBLEM
  for (int i = 0; i < dow; ++i) {
    // 1/tau * u + (u * nabla) u - nu*laplace(u)
    Operator* opL = new Operator(initial_prob.getFeSpace(i), initial_prob.getFeSpace(i));
    addZOT(opL, 1.0/var(tau)); // 1/tau * u
    addSOT(opL, nu);
    initial_prob.addMatrixOperator(opL, i, i);
    
    // 1/tau * u_old
    Operator* opRhs = new Operator(initial_prob.getFeSpace(i));
    addZOT(opRhs, valueOf(u[i])/var(tau)); // 1/tau * u
    initial_prob.addVectorOperator(opRhs, i);
  
    // pressure operators
    Operator* opP = new Operator(initial_prob.getFeSpace(i), initial_prob.getFeSpace(dow));
    addFOT(opP, 1.0, i, GRD_PSI);
    initial_prob.addMatrixOperator(opP, i, dow); 
  
    // divergence operators
    Operator* opDiv = new Operator(initial_prob.getFeSpace(dow), initial_prob.getFeSpace(i));
    addFOT(opDiv, 1.0, i, GRD_PHI);
    initial_prob.addMatrixOperator(opDiv, dow, i); 
  }
  
  for (int i = 0; i < dow; ++i) {
    // ===== add boundary conditions =====
    initial_prob.addDirichletBC(1, i, i, new G(i, vel)); // inflow
    
    for (BoundaryType nr = 3; nr <= 5; ++nr)
      initial_prob.addDirichletBC(nr, i, i, new Constant(0.0)); // wall
  }
  
  double minus1 = -1.0;
  
  for (int i = 0; i < dow; ++i) {
    // 1/tau * u + (u * nabla) u - nu*laplace(u)
    Operator* opL = new Operator(prob.getFeSpace(i), prob.getFeSpace(i));
    addZOT(opL, 1.0/var(tau)); // 1/tau * u
    addSOT(opL, nu);
    for (int j = 0; j < dow; ++j)
      addFOT(opL, valueOf(u[j]), j, GRD_PHI);
    prob.addMatrixOperator(opL, i, i);
    opL->setUhOld(u[i]);
    prob.addVectorOperator(opL, i, &minus1, &minus1);
    
    for (int j = 0; j < dow; ++j) {
      Operator* opN = new Operator(prob.getFeSpace(i), prob.getFeSpace(j));
      addZOT(opN, derivativeOf(u[i], j));
      prob.addMatrixOperator(opN, i, j);
    }
    
    // 1/tau * u_old
    Operator* opRhs = new Operator(prob.getFeSpace(i));
    addZOT(opRhs, valueOf(u_old[i])/var(tau)); // 1/tau * u
    prob.addVectorOperator(opRhs, i);
  
    // pressure operators
    Operator* opP = new Operator(prob.getFeSpace(i), prob.getFeSpace(dow));
    addFOT(opP, 1.0, i, GRD_PSI);
    prob.addMatrixOperator(opP, i, dow); 
    opP->setUhOld(prob.getSolution(dow));
    prob.addVectorOperator(opP, i, &minus1, &minus1);
  
    // divergence operators
    Operator* opDiv = new Operator(prob.getFeSpace(dow), prob.getFeSpace(i));
    addFOT(opDiv, 1.0, i, GRD_PHI);
    prob.addMatrixOperator(opDiv, dow, i); 
    opDiv->setUhOld(prob.getSolution(i));
    prob.addVectorOperator(opDiv, dow, &minus1, &minus1);
  }
  
  for (int i = 0; i < dow; ++i) {
    for (BoundaryType nr = 1; nr <= 5; ++nr)
      if (nr != 2)
	prob.addDirichletBC(nr, i, i, new Constant(0.0)); // wall
  }
  
  // ===== set initial solution =====
  for (int i = 0; i < dow; ++i)
    *u[i] << eval(new G(i, vel));
  
  AdaptInstationary adaptInstat("adapt", prob, adaptInfo, instat, adaptInfo);
  adaptInstat.adapt();

  AMDiS::finalize();
}
