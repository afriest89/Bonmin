// (C) Copyright International Business Machines Corporation and Carnegie Mellon University 2006 
// All Rights Reserved.
// This code is published under the Common Public License.
//
// Authors :
// Andreas Waechter, International Business Machines Corporation
// Pierre Bonami, Carnegie Mellon University,
//
// Date : 02/15/2006


#if defined(_MSC_VER)
// Turn off compiler warning about long names
#  pragma warning(disable:4786)
#endif
#include <iomanip>
#include <fstream>

#include "CoinTime.hpp"
#include "BonminConfig.h"
#include "BonAmplInterface.hpp"
#include "BonIpoptSolver.hpp"
#include "BonCbc.hpp"

#ifdef COIN_HAS_FSQP
#include "BonFilterSolver.hpp"
#endif

using namespace Bonmin;

/** Procedure to ouptut relevant informations in the case of a failure.
    si1 should be the problem solved at a node of the b&b tree, and si2 the original problem.
    Compare the two problems stored in si1 and si2
    and writes files with bounds which have changed.
    Also outputs a file with the starting point of si1.

*/
void writeNodeFiles(const OsiSolverInterface& si1,const OsiTMINLPInterface& si2)
{
  const int numcols = si1.getNumCols();
  const int numrows = si1.getNumRows();
  assert( numcols==si2.getNumCols());
  
  const double * currentLower = si1.getColLower();
  const double * currentUpper = si1.getColUpper();

  const double * originalLower = si2.problem()->orig_x_l();
  const double * originalUpper = si2.problem()->orig_x_u();
  CoinRelFltEq eq;
  std::string fBoundsName;
  si2.getStrParam(OsiProbName,fBoundsName);
  fBoundsName+="_bounds";
  
  std::string fModName = fBoundsName + ".mod";
  std::ofstream fBounds;
  std::ofstream fMod;
  bool hasVarNames = 0;
  
  if(si2.getVarNames()!=NULL )
      hasVarNames=1;
  if(hasVarNames)
    fMod.open(fModName.c_str());
  fBounds.open(fBoundsName.c_str());
    
  for(int i = 0 ; i < numcols ; i++)
    {    
    if(!eq(currentLower[i],originalLower[i]))
      {
        if(hasVarNames)
          fMod<<"bounds"<<i<<": "
	      <<si2.getVarNames()[i]<<" >= "
	      <<currentLower[i]<<";\n";


	fBounds<<"LO"<<"\t"<<i<<"\t"<<currentLower[i]<<std::endl;
    }
    if(!eq(currentUpper[i],originalUpper[i]))
      {
	if(hasVarNames)
	  fMod<<"bounds"<<i<<": "
	      <<si2.getVarNames()[i]<<" <= "
	      <<currentUpper[i]<<";\n";
	
        fBounds<<"UP"<<"\t"<<i<<"\t"<<currentUpper[i]<<std::endl;
      }
    }
  
    //write a file with starting point
    std::string fStartPointName;
    si2.getStrParam(OsiProbName,fStartPointName);
    fStartPointName+="_start";



    const OsiTMINLPInterface* nlpSolver = dynamic_cast<const OsiTMINLPInterface *>(&si1);
    assert(nlpSolver);

    const double * primals = nlpSolver->problem()->x_init();
    const double * duals = nlpSolver->problem()->duals_init();

    if(!primals)//No starting point no output
      {
	std::cerr<<"A failure has occured but no starting point exists"<<std::endl;
	return;
      }

    std::ofstream fStartPoint(fStartPointName.c_str());
    fStartPoint.precision(17);
    fStartPoint<<numcols<<"\t"<<2*numcols+numrows<<std::endl;
    for(int i = 0 ; i < numcols ; i++)
    fStartPoint<<primals[i]<<std::endl;
    int end = 2*numcols + numrows;
    if(duals)
      {
	for(int i = 0 ; i < end; i++)
	  fStartPoint<<duals[i]<<std::endl;
      }

}

int main (int argc, char *argv[])
{
  using namespace Ipopt;
  
  AmplInterface * nlp_and_solver; 

  //We need to build dummy solver objects to get the options, determine which is the solver to use and register all the options
  Ipopt::SmartPtr<IpoptSolver> dummy_ipopt = new IpoptSolver;
  OsiTMINLPInterface forOption(GetRawPtr(dummy_ipopt));


  int solverUsed = 0; // 0 is Ipopt, 1 is Filter
  forOption.solver()->Options()->GetEnumValue("nlp_solver", solverUsed,"bonmin.");

  char * pbName = NULL;
  if(argc > 1)
  {
    pbName = new char[strlen(argv[1])+1];
    strcpy(pbName, argv[1]);
  }
  else //will just output usage
  {
    Ipopt::SmartPtr<IpoptSolver> ipoptSolver = new IpoptSolver;
    nlp_and_solver = new AmplInterface(argv, GetRawPtr(ipoptSolver));
    delete nlp_and_solver;
    return 0;
  }
  double time1 = CoinCpuTime();
  try {
  Ipopt::SmartPtr<TNLPSolver> solver;
  if(solverUsed == 0)
    solver = new IpoptSolver;
  else if(solverUsed == 1)
#ifdef COIN_HAS_FSQP
    solver = new FilterSolver;
#else
    {
      std::cerr<<"filterSQP is not propoerly configured for using into Bonmin"<<std::endl
               <<"be sure to run the configure script with options:"<<std::endl
               <<"--with-filtersqp_lib=\"<path_to_filter_library>\""<<std::endl
               <<"--with-filtersqp_incdir=\"\""<<std::endl;
               throw -1;
      }
#endif
  else
    {
      std::cerr<<"Trying to use unknown solver."<<std::endl;
    }
   nlp_and_solver = new AmplInterface(argv, solver);
    BonminCbcParam par;
    Bab bb;
    par(nlp_and_solver);
    bb(nlp_and_solver, par);//do branch and bound

    std::cout.precision(10);

    std::cout<<pbName<<" \t";
    std::string message;
    std::string status;
    if(bb.mipStatus()==Bab::FeasibleOptimal) {
      status = "\t\"Finished\"";
      message = "\nbonmin: Optimal";
    }
    else if(bb.mipStatus()==Bab::ProvenInfeasible) {
      status = "\t\"Finished\"";
      message = "\nbonmin: Infeasible problem";
    }
    else if(bb.mipStatus()==Bab::Feasible) {
      status = "\t\"Not finished\"";
      message = "\n Optimization not finished.";
    }
    else if(bb.mipStatus()==Bab::NoSolutionKnown) {
      status = "\t\"Not finished\"";
      message = "\n Optimization not finished.";
    }

  if(0)// To output a line for building tables
    std::cout<<status<<"\t"<<CoinCpuTime()-time1<<"\t"
	     <<bb.bestObj()<<"\t"
	     <<bb.numNodes()<<"\t"
	     <<bb.iterationCount()<<"\t"
	     <<nlp_and_solver->totalNlpSolveTime()<<"\t"
	     <<nlp_and_solver->nCallOptimizeTNLP()<<"\t"
	     <<std::endl;
    nlp_and_solver->writeAmplSolFile(message,bb.bestSolution(),NULL);

  }
  catch(TNLPSolver::UnsolvedError *E) {
     E->printError(std::cerr);
    //There has been a failure to solve a problem with Ipopt.
    //And we will output file with information on what has been changed in the problem to make it fail.
    //Now depending on what algorithm has been called (B-BB or other) the failed problem may be at different place.
    //    const OsiSolverInterface &si1 = (algo > 0) ? nlpSolver : *model.solver();
    writeNodeFiles(*nlp_and_solver, *nlp_and_solver);
  }
  catch(OsiTMINLPInterface::SimpleError &E) {
    std::cerr<<E.className()<<"::"<<E.methodName()
	     <<std::endl
	     <<E.message()<<std::endl;
  }
  catch(CoinError &E) {
    std::cerr<<E.className()<<"::"<<E.methodName()
	     <<std::endl
	     <<E.message()<<std::endl;
  }
  catch (Ipopt::OPTION_INVALID &E)
  {
   std::cerr<<"Ipopt exception : "<<E.Message()<<std::endl;
  }
  catch(...) {
    std::cerr<<pbName<<" unrecognized excpetion"<<std::endl;
    std::cerr<<pbName<<"\t Finished \t exception"<<std::endl;
    throw;
  }

  delete [] pbName;
  delete nlp_and_solver;
  return 0;
}

