#include "NuclearStructureManager.h"
#include "NMEOptions.h"
#include "Constants.h"
#include "MatrixElements.h"
#include "NuclearUtilities.h"
#include "ChargeDistributions.h"

#include "boost/algorithm/string.hpp"
#include "gsl/gsl_sf_coupling.h"

namespace NS = NuclearStructure;
namespace NO = NS::nilsson;
namespace ME = NS::MatrixElements;
namespace CD = ChargeDistributions;

using std::cout;
using std::endl;

NS::NuclearStructureManager::NuclearStructureManager() {
  int Zd = GetOpt(int, Daughter.Z);
  int Zm = GetOpt(int, Mother.Z);
  int Ad = GetOpt(int, Daughter.A);
  int Am = GetOpt(int, Mother.A);

  if (Ad != Am) {
    cout << "ERROR: Mother and daughter mass number do not agree." << endl;
    return;
  }
  double Rd = GetOpt(double, Daughter.Radius) * 1e-15 / NATLENGTH *
      std::sqrt(5. / 3.);
  double Rm = GetOpt(double, Mother.Radius) * 1e-15 / NATLENGTH * 
      std::sqrt(5. / 3.);
  if (Rd == 0.0) {
    Rd = 1.2 * std::pow(Ad, 1. / 3.) * 1e-15 / NATLENGTH;
  }
  if (Rm == 0.0) {
    Rm = 1.2 * std::pow(Am, 1. / 3.) * 1e-15 / NATLENGTH;
  }
  double motherBeta2 = GetOpt(double, Mother.Beta2);
  double motherBeta4 = GetOpt(double, Mother.Beta4);
  double motherBeta6 = GetOpt(double, Mother.Beta6);
  double daughterBeta2 = GetOpt(double, Daughter.Beta2);
  double daughterBeta4 = GetOpt(double, Daughter.Beta4);
  double daughterBeta6 = GetOpt(double, Daughter.Beta6);
  int motherSpinParity = GetOpt(int, Mother.SpinParity);
  int daughterSpinParity = GetOpt(int, Daughter.SpinParity);
 
  double motherExcitationEn = GetOpt(double, Mother.ExcitationEnergy);
  double daughterExcitationEn = GetOpt(double, Daughter.ExcitationEnergy);

  std::string process = GetOpt(std::string, Transition.Process);

  if (boost::iequals(process, "B+")) {
    betaType = BETA_PLUS;
  } else {
    betaType = BETA_MINUS;
  }

  if ((Zd - betaType) != Zm) {
    cout << "ERROR: Mother and daughter proton numbers cannot be coupled through process " << process << endl;
    return;
  }

  SetMotherNucleus(Zm, Am, motherSpinParity, Rd, motherExcitationEn, motherBeta2, motherBeta4, motherBeta6);
  SetDaughterNucleus(Zd, Ad, daughterSpinParity, Rm, daughterExcitationEn, daughterBeta2, daughterBeta4, daughterBeta6);

  Initialize(GetOpt(std::string, Computational.Method), GetOpt(std::string, Computational.Potential));
}

NS::NuclearStructureManager::NuclearStructureManager(BetaType bt,
                                                     NS::Nucleus init,
                                                     NS::Nucleus fin) {
  betaType = bt;
  mother = init;
  daughter = fin;
}

void NS::NuclearStructureManager::SetDaughterNucleus(int Z, int A, int dJ,
                                                     double R,
                                                     double excitationEnergy,
                                                     double beta2,
                                                     double beta4, double beta6) {
  daughter = {Z, A, dJ, R, excitationEnergy, beta2, beta4, beta6};
}

void NS::NuclearStructureManager::SetMotherNucleus(int Z, int A, int dJ,
                                                   double R,
                                                   double excitationEnergy,
                                                   double beta2, double beta4,
                                                   double beta6) {
  mother = {Z, A, dJ, R, excitationEnergy, beta2, beta4, beta6};
}

void NS::NuclearStructureManager::Initialize(std::string m, std::string p) {
  method = m;
  potential = p;
  // Extreme Single particle
  if (boost::iequals(method, "ESP")) {
    SingleParticleState spsi, spsf;
    int dKi, dKf;
    double obdme;
    GetESPStates(spsi, spsf, potential, dKi, dKf, obdme);
    AddOneBodyTransition(obdme, dKi, dKf, spsi, spsf);
  }
}

void NS::NuclearStructureManager::GetESPStates(SingleParticleState& spsi,
                                               SingleParticleState& spsf,
                                               std::string potential, int& dKi,
                                               int& dKf, double& obdme) {
  double Vp = GetOpt(double, Computational.Vproton);
  double Vn = GetOpt(double, Computational.Vneutron);
  double Xn = GetOpt(double, Computational.Xneutron);
  double Xp = GetOpt(double, Computational.Xproton);
  double A0 = GetOpt(double, Computational.SurfaceThickness);
  double VSp = GetOpt(double, Computational.V0Sproton);
  double VSn = GetOpt(double, Computational.V0Sneutron);

  int dJReqIn = mother.dJ;
  int dJReqFin = daughter.dJ;
  if (mother.A%2 == 0) {
    dJReqIn = GetOpt(int, Mother.ForcedSPSpin);
    dJReqFin = GetOpt(int, Daughter.ForcedSPSpin);
  }

  double threshold = GetOpt(double, Computational.EnergyMargin);

  double V0p = Vp*(1.+Xp*(mother.A-2.*mother.Z)/mother.A);
  double V0n = Vn*(1.-Xn*(mother.A-2.*mother.Z)/mother.A);

  double mR = mother.R * NATLENGTH * 1e15;
  double dR = daughter.R * NATLENGTH * 1e15;

  if (boost::iequals(potential, "SHO")) {
    int ni, li, si, nf, lf, sf;
    GetESPOrbitalNumbers(ni, li, si, nf, lf, sf);
    WFComp fW = {1.0, nf, lf, sf};
    WFComp iW = {1.0, ni, li, si};
    std::vector<WFComp> fComps = {fW};
    std::vector<WFComp> iComps = {iW};
    spsf = {daughter.dJ, -1, sign(daughter.dJ), lf, nf, 0, -betaType, 0.0, fComps};
    spsi = {mother.dJ, -1, sign(mother.dJ), li, ni, 0, betaType, 0.0, iComps};
  } else {
    double dBeta2 = 0.0, dBeta4 = 0.0, dBeta6 = 0.0, mBeta2 = 0.0, mBeta4 = 0.0, mBeta6 = 0.0;
    if (boost::iequals(potential, "DWS")) {
      dBeta2 = daughter.beta2;
      dBeta4 = daughter.beta4;
      dBeta6 = daughter.beta6;
      mBeta2 = mother.beta2;
      mBeta4 = mother.beta4;
      mBeta6 = mother.beta6;
    }
    if (!GetOpt(bool, Computational.ForceSpin)) {
      threshold = 0.0;
    }
    if (betaType == BETA_MINUS) {
      cout << "Proton State: " << endl;
      spsf =
          NO::CalculateDeformedSPState(daughter.Z, 0, daughter.A, daughter.dJ,
                                       dR, dBeta2, dBeta4, dBeta6, V0p, A0, VSp, dJReqFin, threshold);
      cout << "Neutron State: " << endl;
      spsi = NO::CalculateDeformedSPState(0, mother.A - mother.Z, mother.A,
                                          mother.dJ, mR, mBeta2, mBeta4, mBeta6, V0n,
                                          A0, VSn, dJReqIn, threshold);
    } else {
      cout << "Neutron State: " << endl;
      spsf = NO::CalculateDeformedSPState(0, daughter.A - daughter.Z,
                                          daughter.A, daughter.dJ, dR, dBeta2,
                                          dBeta4, dBeta6, V0n, A0, VSn, dJReqFin, threshold);
      cout << "Proton State: " << endl;
      spsi = NO::CalculateDeformedSPState(mother.Z, 0, mother.A, mother.dJ, mR,
                                          mBeta2, mBeta4, mBeta6, V0p, A0, VSp, dJReqIn, threshold);
    }
  }

  bool reversedGhallagher = GetOpt(bool, Computational.ReversedGhallagher);

  if (boost::iequals(potential, "DWS") && mother.beta2 != 0.0 && daughter.beta2 != 0.0) {
  // Set Omega quantum numbers
  if (mother.A % 2 == 0) {
    int dKc = 0;
    if (((spsi.dO-spsi.lambda) == (spsf.dO-spsf.lambda) && !reversedGhallagher) || ((spsi.dO-spsi.lambda) != (spsf.dO-spsf.lambda) && reversedGhallagher)) {
      dKc = spsi.dO + spsf.dO;
    } else {
      dKc = std::abs(spsi.dO - spsf.dO);
      if (spsi.dO > spsf.dO) {
        spsf.dO *= -1;
      } else {
        spsi.dO *= -1;
      }
    }
    if (mother.Z % 2 == 0) {
      dKi = 0;
      dKf = dKc;
    } else {
      dKf = 0;
      dKi = dKc;
    }
  } else {
    dKi = spsi.dO;
    dKf = spsf.dO;
  }
  }

  if (spsf.parity*spsi.parity*dKi != mother.dJ) {
    cout << "ERROR: Single Particle spin coupling does not match mother spin!" << endl;
    cout << "Single Particle values. First: " << spsi.dO << "/2  Second: " << spsf.dO << "/2" << endl;
    cout << "Coupled spin: " << dKi << " Required: " << mother.dJ << endl;
  }
  if (spsf.parity*spsi.parity*dKf != daughter.dJ) {
    cout << "ERROR: Single Particle spin coupling does not match daughter spin!" << endl;
    cout << "Single Particle values. First: " << spsi.dO << "/2  Second: " << spsf.dO << "/2" << endl;
    cout << "Coupled spin: " << dKf << " Required: " << daughter.dJ << endl;
  }
}

void NS::NuclearStructureManager::AddOneBodyTransition(double obdme, int dKi, int dKf, SingleParticleState spsi, SingleParticleState spsf) {
  OneBodyTransition obt = {obdme, dKi, dKf, spsi, spsf};
  oneBodyTransitions.push_back(obt);
}

void NS::NuclearStructureManager::GetESPOrbitalNumbers(int& ni, int& li,
                                                       int& si, int& nf,
                                                       int& lf, int& sf) {
  std::vector<int> occNumbersInit, occNumbersFinal;
  if (betaType == BETA_MINUS) {
    occNumbersInit = utilities::GetOccupationNumbers(mother.A - mother.Z);
    occNumbersFinal = utilities::GetOccupationNumbers(daughter.Z);
  } else {
    occNumbersInit = utilities::GetOccupationNumbers(mother.Z);
    occNumbersFinal = utilities::GetOccupationNumbers(daughter.A - daughter.Z);
  }
  ni = occNumbersInit[occNumbersInit.size() - 1 - 3];
  li = occNumbersInit[occNumbersInit.size() - 1 - 2];
  si = occNumbersInit[occNumbersInit.size() - 1 - 1];
  nf = occNumbersFinal[occNumbersFinal.size() - 1 - 3];
  lf = occNumbersFinal[occNumbersFinal.size() - 1 - 2];
  sf = occNumbersFinal[occNumbersFinal.size() - 1 - 1];
}

double NS::NuclearStructureManager::GetESPManyParticleCoupling(
    int K, OneBodyTransition& obt) {
  double C = 0.0;

  // TODO Result from Wigner-Eckart in spin space
  int dJi = std::abs(mother.dJ);
  int dJf = std::abs(daughter.dJ);

  if (mother.A % 2 == 0) {
    int dT3i = mother.A - 2 * mother.Z;
    int dT3f = daughter.A - 2 * daughter.Z;
    int dTi = std::abs(dT3i);
    int dTf = std::abs(dT3f);
    if (OptExists(Mother.Isospin)) {
      dTi = GetOpt(int, Mother.Isospin);
    }
    if (OptExists(NuclearPropertiesDaughterIsospin)) {
      dTf = GetOpt(int, Daughter.Isospin);
    }
    /*if ((dJi + dT3i) / 2 % 2 == 0) {
      dTi = dT3i + 1;
    }
    if ((dJf + dT3f) / 2 % 2 == 0) {
      dTf = dT3f + 1;
    }*/

    cout << "Isospin:" << endl;
    cout << "Ti: " << dTi/2 << " Tf: " << dTf/2 << endl;
    cout << "T3: " << dT3i/2 << " T3: " << dT3f/2 << endl;
    // Deformed transition
    if (boost::iequals(potential, "DWS") && mother.beta2 != 0.0 &&
        daughter.beta2 != 0.0) {
      if (mother.Z % 2 == 0) {
        C = 0.5 * std::sqrt((dJi + 1.) * (dJf + 1.) /
                            (1. + delta(obt.dKf, 0.0))) *
            (1 + std::pow(-1., dJi/2.)) * 
            gsl_sf_coupling_3j(dJf, 2 * K, dJi, -obt.dKf, obt.dKf,
                               0);
      } else {
        C = 0.5 * std::sqrt((dJi + 1.) * (dJf + 1.) /
                            (1. + delta(obt.dKi, 0.0))) *
            (1 + std::pow(-1., dJf / 2.)) *
            gsl_sf_coupling_3j(dJf, 2 * K, dJi, 0, -obt.dKi,
                               obt.dKi);
      }
    // Spherical transition
    } else {
      if (mother.Z % 2 == 0) {
        //cout << "BetaType: " << betaType << endl;
        C = std::sqrt((dJi + 1.) * (dJf + 1.) * (dTi + 1.) *
                      (dTf + 1.) / (1. + delta(obt.spsi.dO, obt.spsf.dO))) *
            std::pow(-1., (dTf - dT3f) / 2.) *
            gsl_sf_coupling_3j(dTf, 2, dTi, -dT3f, -2 * betaType, dT3i) *
            gsl_sf_coupling_6j(1, dTf, (dTf+dTi)/2, dTi, 1, 2) * std::sqrt(3. / 2.) *
            std::pow(-1., K) * 2 * (delta(obt.spsi.dO, obt.spsf.dO) -
                                    std::pow(-1., (obt.spsi.dO + obt.spsf.dO) / 2.)) *
            gsl_sf_coupling_6j(obt.spsf.dO, dJf, obt.spsi.dO, dJi,
                               obt.spsi.dO, 2 * K);
      } else {
        C = std::sqrt((dJi + 1.) * (dJf + 1.) * (dTi + 1.) *
                      (dTf + 1.) / (1. + delta(obt.spsi.dO, obt.spsf.dO))) *
            std::pow(-1., (dTf - dT3f) / 2.) *
            gsl_sf_coupling_3j(dTf, 2, dTi, -dT3f, -2 * betaType, dT3i) *
            gsl_sf_coupling_6j(1, dTf, (dTf+dTi)/2, dTi, 1, 2) * std::sqrt(3. / 2.) *
            std::pow(-1., K) * 2 * (1 + delta(obt.spsi.dO, obt.spsi.dO)) *
            gsl_sf_coupling_6j(obt.spsf.dO, dJf, obt.spsf.dO, dJi,
                               obt.spsi.dO, 2 * K);
      }
    }
  } else {
    //cout << "Odd-A decay" << endl;
    if (boost::iequals(potential, "DWS") && mother.beta2 != 0.0 &&
        daughter.beta2 != 0.0) {
      //cout << "Deformed: " << endl;
      C = std::sqrt((dJi + 1.) * (dJf + 1.) /
                    (1. + delta(obt.dKi, 0)) / (1. + delta(obt.dKf, 0)));
    } else {
      // TODO check
      C = std::sqrt(4. * M_PI / (dJi + 1.));
      C = 1.;
    }
  }
  return C;
}

double NS::NuclearStructureManager::CalculateMatrixElement(bool V, int K, int L,
                                                           int s) {
  double result = 0.0;
  double nu = CD::CalcNu(mother.R * std::sqrt(3. / 5.), mother.Z);
  int opt = 0;

  if (mother.A % 2 == 0) {
    if (mother.Z % 2 == 1) {
      opt = 1;
    } else {
      opt = 2;
    }
  }

  //cout << "opt: " << opt << endl;

  for (int i = 0; i < oneBodyTransitions.size(); i++) {
    OneBodyTransition obt = oneBodyTransitions[i];
    if (boost::iequals(method, "ESP")) {
      obt.obdme = GetESPManyParticleCoupling(K, obt);
    }
    cout << "OBDME: " << obt.obdme << endl;
    if (boost::iequals(potential, "DWS") && mother.beta2 != 0 &&
        daughter.beta2 != 0) {
      cout << "Deformed" << endl;
      result += obt.obdme * ME::GetDeformedSingleParticleMatrixElement(
                                opt, obt.spsi, obt.spsf, V, K, L, s, std::abs(mother.dJ),
                                std::abs(daughter.dJ), obt.dKi, obt.dKf, mother.R, nu);
    } else {
      result += obt.obdme * ME::GetSingleParticleMatrixElement(
                                V, std::abs(mother.dJ) / 2., K, L, s, obt.spsi,
                                obt.spsf, mother.R, nu);
    }
  }
  return result;
}

double NS::NuclearStructureManager::CalculateWeakMagnetism() {
  double result = 0.0;

  double gM = GetOpt(double, Constants.gM);
  double gA = GetOpt(double, Constants.gA);

  double VM111 = CalculateMatrixElement(true, 1, 1, 1);
  double AM101 = CalculateMatrixElement(false, 1, 0, 1);

  //cout << "AM101: " << AM101 << " VM111: " << VM111 << endl;

  result = -std::sqrt(2. / 3.) * nucleonMasskeV / electronMasskeV * mother.R /
               gA * VM111 / AM101 +
           gM / gA;
  return result;
}

double NS::NuclearStructureManager::CalculateInducedTensor() {
  double result = 0.0;

  double AM110 = CalculateMatrixElement(false, 1, 1, 0);
  double AM101 = CalculateMatrixElement(false, 1, 0, 1);

  result = 2. / std::sqrt(3.) * nucleonMasskeV / electronMasskeV * mother.R *
           AM110 / AM101;
  return result;
}
