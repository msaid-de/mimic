/*******************************************************************************
    Copyright 2006-2009 Lukas Käll <lukas.kall@cbr.su.se>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

 *******************************************************************************/
#include <map>
#include <set>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <math.h>
#include <assert.h>
using namespace std;
#include "AminoAcidDist.h"
#include "Option.h"
#include "Peptides.h"

string Peptides::proteinNamePrefix_ = "mimic|Random_";
// N.B.: the shortest shuffled peptide will be minLen + 2, as we conserve the 
// first and last AA of each peptide
unsigned int Peptides::minLen = 4;
unsigned int Peptides::multFactor_ = 1;
double Peptides::sharedPeptideRatio_ = 0.0;

AminoAcidDist Peptides::background = AminoAcidDist();

Peptides::Peptides() : replaceI(false) {}
Peptides::~Peptides() {}

void Peptides::printAll(const vector<string>& connectorStrings, const std::string& suffix) {
  unsigned int count = 0;
  vector<string> outPep(connectorStrings.size());
  map<string,set<unsigned int> >::const_iterator it = pep2ixs.begin();
  for (; it != pep2ixs.end(); it++) {
    set<unsigned int>::const_iterator ixIt = it->second.begin();
    for (; ixIt != it->second.end(); ixIt++) {  
      assert(outPep[*ixIt] == "");
      outPep[*ixIt] = it->first;
      count++;
    }
  }
  assert(count == connectorStrings.size());
  ostringstream first("");
  for (size_t ix = 0; ix < connectorStrings.size(); ix++) {
    if (connectorStrings[ix][0] == '>') {
      string str = first.str();
      size_t p = 0;
      while (p < str.length()) {
        cout << str.substr(p,lineLen) << endl;
        p+=lineLen;
      }
      first.str("");
      cout << connectorStrings[ix] << suffix << endl;
    } else {
      first << connectorStrings[ix];
    }
    first << outPep[ix];
  }
  string str=first.str();
  size_t p=0;
  while(p<str.length()) {
    cout << str.substr(p,lineLen) << endl;
    p+=lineLen;
  }
  first.str("");
}

void Peptides::addPeptide(const string& peptide, unsigned int pepNo) {
  assert(pep2ixs[peptide].count(pepNo) == 0);
  pep2ixs[peptide].insert(pepNo);
  assert(pepNo+1 == connectorStrings_.size());
}

void Peptides::cleaveProtein(string seq, unsigned int& pepNo) {
  size_t lastPos = 0, pos = 0, protLen = seq.length();
  if (protLen > 0 && seq[protLen-1]=='*') {
    --protLen;
  }
  for (; pos<protLen; pos++) {
    if (pos == 0 || seq[pos] == 'K' || seq[pos] == 'R') {
      // store peptide without C-terminal 'K/R'
      if (pos > lastPos) {
        addPeptide(seq.substr(lastPos,pos-lastPos), pepNo++);
      }
      
      // store single/multiple consecutive 'K/R' to leave unchanged in 
      // shuffled protein sequence
      int len = 1;
      while (pos+len+1 < protLen && (seq[pos+len-1] == 'K' || seq[pos+len-1] == 'R')) {
        len++;
      }
      connectorStrings_.push_back(seq.substr(pos,len));
      
      // update position to jump over 'K/R' region
      lastPos = pos + len;
      pos += len - 1;
    }
  }
  
  // store protein C-terminal peptide
  if (pos > lastPos) {
    addPeptide(seq.substr(lastPos,pos-lastPos), pepNo++);
  }
}


void Peptides::readFasta(string path) {
  ifstream fastaIn(path.c_str(), ios::in);
  string line(""), pep(""), seq("");
  unsigned int pepNo = 0,proteinNo = 0;
  bool spillOver = false;
  while (getline(fastaIn, line)) {
    if (line[0] == '>') { // id line
      if (spillOver) {
        assert(seq.size() > 0);
        // add peptides and KR-stretches to connectorStrings_
        cleaveProtein(seq, pepNo);
        seq = "";
      }
      ostringstream newProteinName("");
      newProteinName << ">" << proteinNamePrefix_ << ++proteinNo;
      connectorStrings_.push_back(newProteinName.str());
      assert(connectorStrings_.size() == pepNo+1);
    } else { // amino acid sequence
      seq += line;
      spillOver = true;
    }
  } 
  if (spillOver) {
    cleaveProtein(seq, pepNo);
  }
}

void Peptides::shuffle(const string& in,string& out) {
  out=in;
  int i = in.length();
  if (i==0) return;
  // fisher_yates_shuffle
  for (; --i; ) {
    double d=((double)(i+1)*((double)rand()/((double)RAND_MAX+(double)1)));
    int j=(int)d;
    if (i!=j){
      char a = out[i];
      out[i] = out[j];
      out[j] = a;
    }
  }
}
void Peptides::mutate(const string& in, string& out) {
  out=in;
  int i = in.length();
  int j=(int)((double)(i+1)*((double)rand()/((double)RAND_MAX+(double)1)));
  double d=((double)rand()/((double)RAND_MAX+(double)1));
  out[j] = background.generateAA(d);
}

bool Peptides::checkAndMarkUsedPeptide(const string& pep, bool force) {
  string checkPep=pep;
  if (replaceI) {
    for(string::iterator it=checkPep.begin();it!=checkPep.end();it++) {
      if (*it=='I')
        *it='L';
      if (*it=='N')
        *it='L';
      if (*it=='Q')
        *it='E';
      if (*it=='K')
        *it='E';
    }
  }
  bool used = usedPeptides_.count(checkPep) > 0;
  if (force || !used)
    usedPeptides_.insert(checkPep);
  return used;
}


void Peptides::shuffle(const map<string,set<unsigned int> >& normalPep2ixs) {
  map<string,set<unsigned int> >::const_iterator it = normalPep2ixs.begin();
  bool force = true;
  for(; it != normalPep2ixs.end(); it++) {
    checkAndMarkUsedPeptide(it->first, force);
  }
  
  it = normalPep2ixs.begin();
  for (; it != normalPep2ixs.end(); it++) {
    double uniRand = ( (double)rand() / ((double)RAND_MAX+(double)1) );
    string scrambledPeptide = it->first;
    if (uniRand >= sharedPeptideRatio_) {
      size_t tries = 0;
      bool peptideUsed;
      do {
        shuffle(it->first, scrambledPeptide);
        peptideUsed = checkAndMarkUsedPeptide(scrambledPeptide);
      } while ( peptideUsed && 
               (it->first).length() >= minLen &&  
               ++tries < maxTries);
      
      // if scrambling does not give a usable peptide, mutate some AAs
      if (tries == maxTries) {
        tries = 0;
        scrambledPeptide = it->first;
        string mutatedPeptide;
        do {
          mutate(scrambledPeptide, mutatedPeptide);
          peptideUsed = checkAndMarkUsedPeptide(mutatedPeptide);
          scrambledPeptide = mutatedPeptide;
        } while ( peptideUsed && ++tries < maxTries);
      }
      //if (tries == maxTries) cerr << "Gave up on peptide " << it->first << endl;
    }
    pep2ixs[scrambledPeptide].insert(it->second.begin(),it->second.end());
  }
}

int Peptides::run() {
  srand(time(0));
  cerr << "Reading fasta file and in-silico digesting proteins" << endl;
  readFasta(inFile);
  for (unsigned int m = 0; m < multFactor_; ++m) {
    Peptides entrapmentDB;
    
    cerr << "Shuffling round: " << (m+1) << endl;
    entrapmentDB.shuffle(pep2ixs);
    
    ostringstream suffix("");
    if (multFactor_ > 1) {
      suffix << "|shuffle_" << (m+1);
    }
    
    entrapmentDB.printAll(connectorStrings_, suffix.str());
  }
  return 0;
}

bool Peptides::parseOptions(int argc, char **argv){
  ostringstream intro;
  intro << "Usage:" << endl;
  intro << "   mimic <fasta-file>" << endl;
  CommandLineParser cmd(intro.str());
  
  cmd.defineOption("p",
      "prefix",
      "Prefix to mimic proteins (Default: \"mimic|Random_\")",
      "string");
  cmd.defineOption("m",
      "mult-factor",
      "Number of times the database should be multiplied (Default: 1)",
      "int");
  cmd.defineOption("s",
      "shared-pept-ratio",
      "Ratio of shared peptides that will stay preserved in the mimic database (Default: 0.0)",
      "int");
  
  cmd.parseArgs(argc, argv);
  
  if (cmd.optionSet("p")) {
    Peptides::proteinNamePrefix_ = cmd.options["p"];
  }
  if (cmd.optionSet("m")) {
    Peptides::multFactor_ = cmd.getInt("m", 1, 1000);
  }
  if (cmd.optionSet("s")) {
    Peptides::sharedPeptideRatio_ = cmd.getDouble("s", 0.0, 1.0);
  }
  
  if (cmd.arguments.size() > 0) {
    inFile = cmd.arguments[0];
  } else {
    std::cerr << "No fasta file specified" << std::endl;
    return false;
  }
  return true;
}


int main(int argc, char **argv){
  Peptides *pPeptides=new Peptides();
  int retVal=-1;
  if(pPeptides->parseOptions(argc,argv))
  {
    retVal=pPeptides->run();
  }
  delete pPeptides;
  return retVal;
}   


