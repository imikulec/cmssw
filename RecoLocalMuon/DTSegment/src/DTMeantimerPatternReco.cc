/** 
 *
 * \author Stefano Lacaprara - INFN Legnaro <stefano.lacaprara@pd.infn.it>
 * \author Riccardo Bellan - INFN TO <riccardo.bellan@cern.ch>
 * \author Piotr Traczyk - SINS Warsaw <ptraczyk@fuw.edu.pl>
 */

/* This Class Header */
#include "RecoLocalMuon/DTSegment/src/DTMeantimerPatternReco.h"

/* Collaborating Class Header */
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "DataFormats/Common/interface/OwnVector.h"
#include "DataFormats/GeometryVector/interface/GlobalPoint.h"
#include "Geometry/Records/interface/MuonGeometryRecord.h"
#include "Geometry/DTGeometry/interface/DTGeometry.h"
#include "Geometry/DTGeometry/interface/DTLayer.h"
#include "Geometry/DTGeometry/interface/DTSuperLayer.h"
#include "DataFormats/DTRecHit/interface/DTSLRecSegment2D.h"
#include "RecoLocalMuon/DTSegment/src/DTSegmentUpdator.h"
#include "RecoLocalMuon/DTSegment/src/DTSegmentCleaner.h"
#include "RecoLocalMuon/DTSegment/src/DTHitPairForFit.h"
#include "RecoLocalMuon/DTSegment/src/DTSegmentCand.h"
#include "RecoLocalMuon/DTSegment/src/DTLinearFit.h"

/* C++ Headers */
#include <iterator>
using namespace std;
#include "FWCore/ParameterSet/interface/ParameterSet.h"

/* ====================================================================== */

  typedef std::vector<std::shared_ptr<DTHitPairForFit>> hitCont;
  typedef hitCont::const_iterator  hitIter;


/// Constructor
DTMeantimerPatternReco::DTMeantimerPatternReco(const edm::ParameterSet& pset) : 
  DTRecSegment2DBaseAlgo(pset), 
  theFitter(new DTLinearFit()),
  theAlgoName("DTMeantimerPatternReco")
{
  theMaxAllowedHits = pset.getParameter<unsigned int>("MaxAllowedHits"); // 100
  theAlphaMaxTheta = pset.getParameter<double>("AlphaMaxTheta");// 0.1 ;
  theAlphaMaxPhi = pset.getParameter<double>("AlphaMaxPhi");// 1.0 ;
  theMaxChi2 = pset.getParameter<double>("MaxChi2");// 8.0 ;
  debug = pset.getUntrackedParameter<bool>("debug");
  theUpdator = new DTSegmentUpdator(pset);
  theCleaner = new DTSegmentCleaner(pset);
}

/// Destructor
DTMeantimerPatternReco::~DTMeantimerPatternReco() {
  delete theUpdator;
  delete theCleaner;
  delete theFitter;
}

/* Operations */ 
edm::OwnVector<DTSLRecSegment2D>
DTMeantimerPatternReco::reconstruct(const DTSuperLayer* sl,
                                        const std::vector<DTRecHit1DPair>& pairs){

  edm::OwnVector<DTSLRecSegment2D> result;
  std::vector<std::shared_ptr<DTHitPairForFit>> hitsForFit = initHits(sl, pairs);

  vector<DTSegmentCand*> candidates = buildSegments(sl, hitsForFit);

  vector<DTSegmentCand*>::const_iterator cand=candidates.begin();
  while (cand<candidates.end()) {
    
    DTSLRecSegment2D *segment = (**cand);
    theUpdator->update(segment,1);

    if (debug) 
      cout<<"Reconstructed 2D segments "<<*segment<<endl;
    result.push_back(segment);
    
    delete *(cand++); // delete the candidate!
  }

  return result;
}

void DTMeantimerPatternReco::setES(const edm::EventSetup& setup){
  // Get the DT Geometry
  setup.get<MuonGeometryRecord>().get(theDTGeometry);
  theUpdator->setES(setup);
}

vector<std::shared_ptr<DTHitPairForFit>>
DTMeantimerPatternReco::initHits(const DTSuperLayer* sl,
                                     const std::vector<DTRecHit1DPair>& hits){  
  
  hitCont result;
  for (vector<DTRecHit1DPair>::const_iterator hit=hits.begin();
       hit!=hits.end(); ++hit) {
    result.push_back(std::make_shared<DTHitPairForFit>(*hit, *sl, theDTGeometry));
  }
  return result;
}

vector<DTSegmentCand*>
DTMeantimerPatternReco::buildSegments(const DTSuperLayer* sl,
                                      const std::vector<std::shared_ptr<DTHitPairForFit>>& hits){

  vector<DTSegmentCand*> result;
  DTEnums::DTCellSide codes[2]={DTEnums::Left, DTEnums::Right};

  if(debug) {
    cout << "buildSegments: " << sl->id() << " nHits " << hits.size() << endl;
    for (hitIter hit=hits.begin(); hit!=hits.end(); ++hit) cout << **hit<< " wire: " << (*hit)->id() << " DigiTime: " << (*hit)->digiTime() << endl;
  }

  if (hits.size() > theMaxAllowedHits ) {
    if(debug) {
      cout << "Warning: this SuperLayer " << sl->id() << " has too many hits : "
        << hits.size() << " max allowed is " << theMaxAllowedHits << endl;
      cout << "Skipping segment reconstruction... " << endl;
    }
    return result;
  }

  GlobalPoint IP;
  float DAlphaMax;
  if ((sl->id()).superlayer()==2)  // Theta SL
    DAlphaMax=theAlphaMaxTheta;
  else // Phi SL
    DAlphaMax=theAlphaMaxPhi;
  
  // get two hits in different layers and see if there are other hits
  //  compatible with them
  for (hitCont::const_iterator firstHit=hits.begin(); firstHit!=hits.end();
       ++firstHit) {
    for (hitCont::const_reverse_iterator lastHit=hits.rbegin(); 
         (*lastHit)!=(*firstHit); ++lastHit) {
      
      // a geometrical sensibility cut for the two hits
      if (!geometryFilter((*firstHit)->id(),(*lastHit)->id())) continue;
	 
      // create a set of hits for the fit (only the hits between the two selected ones)
      hitCont hitsForFit;
      for (hitCont::const_iterator tmpHit=firstHit+1; (*tmpHit)!=(*lastHit); tmpHit++) 
        if ((geometryFilter((*tmpHit)->id(),(*lastHit)->id())) 
            && (geometryFilter((*tmpHit)->id(),(*firstHit)->id()))) hitsForFit.push_back(*tmpHit);

      for (int firstLR=0; firstLR<2; ++firstLR) {
        for (int lastLR=0; lastLR<2; ++lastLR) {

	  // TODO move the global transformation in the DTHitPairForFit class
	  // when it will be moved I will able to remove the sl from the input parameter
	  GlobalPoint gposFirst=sl->toGlobal( (*firstHit)->localPosition(codes[firstLR]) );
	  GlobalPoint gposLast= sl->toGlobal( (*lastHit)->localPosition(codes[lastLR]) );
          GlobalVector gvec=gposLast-gposFirst;
          GlobalVector gvecIP=gposLast-IP;

          // difference in angle measured
          float DAlpha=fabs(gvec.theta()-gvecIP.theta());
          if (DAlpha>DAlphaMax) continue;
	  
//	  if(debug) {
//              cout << "Selected hit pair:" << endl;
//              cout << "  First " << *(*firstHit) << " Layer Id: " << (*firstHit)->id().layerId() << " Side: " << firstLR << " DigiTime: " << (*firstHit)->digiTime() << endl;
//              cout << "  Last "  << *(*lastHit)  << " Layer Id: " << (*lastHit)->id().layerId()  << " Side: " << lastLR << " DigiTime: " << (*lastHit)->digiTime() <<  endl;
//          }
        
          vector<DTSegmentCand::AssPoint> assHits;
          // create a candidate hit list
          assHits.push_back(DTSegmentCand::AssPoint(*firstHit,codes[firstLR]));
          assHits.push_back(DTSegmentCand::AssPoint(*lastHit,codes[lastLR]));

          // run hit adding/segment building 
          maxfound = 3;
          addHits(sl,assHits,hitsForFit,result);
        }
      }
    }
   }

  // now I have a couple of segment hypotheses, should check for ghosts
  if (debug) {
    cout << "Result (before cleaning): " << result.size() << endl;
    for (vector<DTSegmentCand*>::const_iterator seg=result.begin(); seg!=result.end(); ++seg) cout << *(*seg) << endl;
  }        

  result = theCleaner->clean(result);

  if (debug) {
    cout << "Result (after cleaning): " << result.size() << endl;
    for (vector<DTSegmentCand*>::const_iterator seg=result.begin(); seg!=result.end(); ++seg) cout << *(*seg) << endl;
  }

  return result;
}



void 
DTMeantimerPatternReco::addHits(const DTSuperLayer* sl, vector<DTSegmentCand::AssPoint>& assHits, const vector<std::shared_ptr<DTHitPairForFit>>& hits, 
                                vector<DTSegmentCand*> &result) {

  double chi2l,chi2r,t0_corrl,t0_corrr;
  bool foundSomething = false;

//  if (debug) 
//    cout << "DTMeantimerPatternReco::addHit " << endl << "   Picked " << assHits.size() << " hits, " << hits.size() << " left." << endl;
  
  if (assHits.size()+hits.size()<maxfound) return;

  // loop over the remaining hits
  for (hitCont::const_iterator hit=hits.begin(); hit!=hits.end(); ++hit) {

//    if (debug) {
//      int nHits=assHits.size()+1;
//      cout << "     Trying B: " << **hit<< " wire: " << (*hit)->id() << endl;
//      printPattern(assHits,*hit);
//    }

    assHits.push_back(DTSegmentCand::AssPoint(*hit, DTEnums::Left));
    std::unique_ptr<DTSegmentCand> left_seg = fitWithT0(sl,assHits, chi2l, t0_corrl,0);
    assHits.pop_back();
//    if (debug) 
//      cout << "    Left:  t0= " << t0_corrl << "  chi2/nHits= " << chi2l << "/" << nHits << "  ok: " << left_ok << endl;

    assHits.push_back(DTSegmentCand::AssPoint(*hit, DTEnums::Right));
    std::unique_ptr<DTSegmentCand> right_seg = fitWithT0(sl,assHits, chi2r, t0_corrr,0);
    assHits.pop_back();
//    if (debug) 
//      cout << "   Right:  t0= " << t0_corrr << "  chi2/nHits= " << chi2r << "/" << nHits << "  ok: " << right_ok << endl;

    bool left_ok=(left_seg)?true:false;
    bool right_ok=(right_seg)?true:false;

    if (!left_ok && !right_ok) continue;

    foundSomething = true;    

    // prepare the hit set for the next search, start from the other side                        
    hitCont hitsForFit;
    for (hitCont::const_iterator tmpHit=hit+1; tmpHit!=hits.end(); tmpHit++) 
      if (geometryFilter((*tmpHit)->id(),(*hit)->id())) hitsForFit.push_back(*tmpHit); 

    reverse(hitsForFit.begin(),hitsForFit.end());

    // choose only one - left or right
    if (assHits.size()>3 && left_ok && right_ok) {
      if (chi2l<chi2r-0.1) right_ok=false; else
        if (chi2r<chi2l-0.1) left_ok=false;
    }
    if (left_ok) { 
      assHits.push_back(DTSegmentCand::AssPoint(*hit, DTEnums::Left));
      addHits(sl,assHits,hitsForFit,result);
      assHits.pop_back();
    }
    if (right_ok) { 
      assHits.push_back(DTSegmentCand::AssPoint(*hit, DTEnums::Right));
      addHits(sl,assHits,hitsForFit,result);
      assHits.pop_back();
    }
  }

  if (foundSomething) return;
  // if we didn't find any new hits compatible with the current candidate, we proceed to check and store the candidate

  // If we already have a segment with more hits from this hit pair - don't save this one.  
  if (assHits.size()<maxfound) return;

  // Check if semgent Ok, calculate chi2
  std::unique_ptr<DTSegmentCand> seg = fitWithT0(sl,assHits, chi2l, t0_corrl,debug);
  if (!seg) return;
  
  if (!seg->good()) {
//    if (debug) cout << "   Segment not good() - skipping" << endl;
    return;
  }

  if (assHits.size()>maxfound) maxfound = assHits.size();
  if (debug) cout << endl << "   Seg t0= " << t0_corrl << *seg<< endl;
  
  if (checkDoubleCandidates(result,seg.get())) {
    result.push_back(seg.release());
    if (debug) cout << "   Result is now " << result.size() << endl;
  } else {
    if (debug) cout << "   Exists - skipping" << endl;
  }
}


bool
DTMeantimerPatternReco::geometryFilter( const DTWireId first, const DTWireId second ) const
{
//  return true;

  const int layerLowerCut[4]={0,-1,-2,-2};
  const int layerUpperCut[4]={0, 2, 2, 3};
//  const int layerLowerCut[4]={0,-2,-4,-5};
//  const int layerUpperCut[4]={0, 3, 4, 6};

  // deal only with hits that are in the same SL
  if (first.layerId().superlayerId().superLayer()!=second.layerId().superlayerId().superLayer()) 
    return true;
    
  int deltaLayer=abs(first.layerId().layer()-second.layerId().layer());

  // drop hits in the same layer
  if (!deltaLayer) return false;

  // protection against unexpected layer numbering
  if (deltaLayer>3) { 
    cout << "*** WARNING! DT Layer numbers differ by more than 3! for hits: " << endl;
    cout << "             " << first << endl;
    cout << "             " << second << endl;
    return false;
  }

  // accept only hits in cells "not too far away"
  int deltaWire=first.wire()-second.wire();
  if (second.layerId().layer()%2==0) deltaWire=-deltaWire; // yet another trick to get it right...
  if ((deltaWire<=layerLowerCut[deltaLayer]) || (deltaWire>=layerUpperCut[deltaLayer])) return false;

  return true;
}


std::unique_ptr<DTSegmentCand>
DTMeantimerPatternReco::fitWithT0(const DTSuperLayer* sl, const vector<DTSegmentCand::AssPoint> &assHits, double &chi2, double &t0_corr, const bool fitdebug) 
{
  // create a DTSegmentCand 
  DTSegmentCand::AssPointCont pointsSet;
  pointsSet.insert(assHits.begin(),assHits.end());
  std::unique_ptr<DTSegmentCand> seg(new DTSegmentCand(pointsSet,sl));
  
  // perform the 3 parameter fit on the segment candidate
  theUpdator->fit(seg.get(),1,fitdebug);

  chi2 = seg->chi2();
  t0_corr = seg->t0();
  
  // sanity check - drop segment candidates with a failed fit
  // for a 3-par fit this includes segments with hits after the calculated t0 correction ending up
  // beyond the chamber walls or on the other side of the wire
  if (chi2==-1.) return nullptr;

  // at this point we keep all 3-hit segments that passed the above check
  if (assHits.size()==3) return seg;

  // for segments with no t0 information we impose a looser chi2 cut
  if (t0_corr==0) {
    if (chi2<200.) return seg;
      else return nullptr; 
  }

  // cut on chi2/ndof of the segment candidate
  if ((chi2/(assHits.size()-3)<theMaxChi2)) return seg;
    else return nullptr;
}



bool
DTMeantimerPatternReco::checkDoubleCandidates(vector<DTSegmentCand*>& cands,
                                              DTSegmentCand* seg) {
  for (vector<DTSegmentCand*>::iterator cand=cands.begin();
       cand!=cands.end(); ++cand) {
    if (*(*cand)==*seg) return false;
    if (((*cand)->nHits()>=seg->nHits()) && ((*cand)->chi2ndof()<seg->chi2ndof()))
      if ((*cand)->nSharedHitPairs(*seg)>int(seg->nHits()-2)) return false;
  }    
  return true;
}



void 
DTMeantimerPatternReco::printPattern( vector<DTSegmentCand::AssPoint>& assHits, const DTHitPairForFit* hit) {

  char mark[26]={". . . . . . . . . . . . "};
  int wire[12]={0,0,0,0,0,0,0,0,0,0,0,0};

  for (vector<DTSegmentCand::AssPoint>::const_iterator assHit=assHits.begin(); assHit!=assHits.end(); ++assHit) {
    int lay  = (((*assHit).first)->id().superlayerId().superLayer()-1)*4 + ((*assHit).first)->id().layerId().layer()-1;
    wire[lay]= ((*assHit).first)->id().wire();
    if ((*assHit).second==DTEnums::Left) mark[lay*2]='L'; else mark[lay*2]='R';
  }

  int lay  = ((*hit).id().superlayerId().superLayer()-1)*4 + (*hit).id().layerId().layer() - 1;
  wire[lay]= (*hit).id().wire();
  mark[lay*2]='*';

  cout << "   " << mark << endl << "  ";
  for (int i=0; i<12; i++) if (wire[i]) cout << setw(2) << wire[i]; else cout << "  ";
  cout << endl;
}
