/*
   This file is part of the Eagle haplotype phasing software package
   developed by Po-Ru Loh.  Copyright (C) 2015-2016 Harvard University.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#include <htslib/vcf.h>
#include <htslib/synced_bcf_reader.h>

#include "Types.hpp"
#include "MemoryUtils.hpp"
#include "MapInterpolater.hpp"
#include "SyncedVcfData.hpp"

namespace EAGLE {

  using std::vector;
  using std::string;
  using std::pair;
  using std::make_pair;
  using std::cout;
  using std::cerr;
  using std::endl;

  void process_ref_genotypes(int nsmpl, int ngt, int32_t *gt, bool refAltSwap,
			     vector <bool> &hapsRef, int &numMissing, int &numUnphased, uint &w) {
    numMissing = numUnphased = 0;
    if (ngt != 2*nsmpl) {
      cerr << "ERROR: ref ploidy != 2 (ngt != 2*nsmpl): ngt="
	   << ngt << ", nsmpl=" << nsmpl << endl;
      exit(1);
    }
    int ploidy = ngt/nsmpl;
    for (int i=0; i<nsmpl; i++)
      {
	int32_t *ptr = gt + i*ploidy;
	bool haps[2]; bool missing = false, unphased = false;
	for (int j=0; j<ploidy; j++)
	  {
	    if ( ptr[j]==bcf_int32_vector_end ) { // this sample is haploid if ploidy==2
	      cerr << "ERROR: ref genotypes contain haploid sample" << endl;
	      exit(1);
	    }
	    if ( bcf_gt_is_missing(ptr[j]) ) { // missing allele
	      missing = true;
	    }
	    else {
	      int idx = bcf_gt_allele(ptr[j]); // allele index
	      haps[j] = (idx >= 1); // encode REF allele -> 0, ALT allele(s) -> 1
	      if ( j==1 && !bcf_gt_is_phased(ptr[j]) ) unphased = true;
	    }
	  }
	if (missing) {
	  haps[0] = haps[1] = 0; // set both alleles to REF allele
	  numMissing++;
	}
	else if (unphased) {
	  if (haps[0] != haps[1] && ((w=18000*(w&65535)+(w>>16))&1))
	    std::swap(haps[0], haps[1]); // randomize phasing
	  numUnphased++;
	}
	if (refAltSwap) { // target REF/ALT are swapped relative to reference REF/ALT
	  haps[0] = !haps[0];
	  haps[1] = !haps[1];
	}
	hapsRef.push_back(haps[0]);
	hapsRef.push_back(haps[1]);	  
      }
  }

  void process_target_genotypes(int nsmpl, int ngt, int32_t *gt, vector <uchar> &genosTarget,
				int &numMissing) {
    numMissing = 0;
    if (ngt != 2*nsmpl) {
      cerr << "ERROR: target ploidy != 2 (ngt != 2*nsmpl): ngt="
	   << ngt << ", nsmpl=" << nsmpl << endl;
      exit(1);
    }
    int ploidy = ngt/nsmpl;
    for (int i=0; i<nsmpl; i++)
      {
	int32_t *ptr = gt + i*ploidy;
	bool missing = false;
	uchar g = 0;
	for (int j=0; j<ploidy; j++)
	  {
	    if ( ptr[j]==bcf_int32_vector_end ) { // this sample is haploid if ploidy==2
	      cerr << "ERROR: target genotypes contain haploid sample" << endl;
	      exit(1);
	    }
	    if ( bcf_gt_is_missing(ptr[j]) ) { // missing allele
	      missing = true;
	    }
	    else {
	      int idx = bcf_gt_allele(ptr[j]); // allele index
	      if (idx > 1) {
		cerr << "ERROR: multi-allelic site found in target; should have been filtered"
		     << endl;
		exit(1);
	      }
	      g += idx;
	    }
	  }
	if (missing) {
	  g = 9;
	  numMissing++;
	}
	genosTarget.push_back(g);
      }
  }

  vector < pair <int, int> > SyncedVcfData::processVcfs
  (const string &vcfRef, const string &vcfTarget, bool allowRefAltSwap, int chrom, double bpStart,
   double bpEnd, vector <bool> &hapsRef, vector <uchar> &genosTarget, const string &tmpFile,
   const string &writeMode) {

    vector < pair <int, int> > chrBps;

    bcf_srs_t *sr = bcf_sr_init();
    sr->require_index = 1;

    if ( chrom!=0 )
    {
        kstring_t str = {0,0,0};
        ksprintf(&str,"%d:%d-%d",chrom,(uint32_t)bpStart,(uint32_t)bpEnd);
        if ( bcf_sr_set_regions(sr, str.s, 0)!=0 )
        {
            cerr << "ERROR: failed to initialize the region:" << str.s;
            exit(1);
        }
        free(str.s);
    }

    // By default, the synced reader requires that CHR, POS and ALT are the same
    // in both files. If this is too strict and SNP/indel/all lines with the same
    // position should be considered as matching, uncomment:
    //
    //      sr->collapse = COLLAPSE_SNPS|COLLAPSE_INDELS;
    //
    // See also examples in bcftools/vcfisec etc.

    if (allowRefAltSwap)
      sr->collapse = COLLAPSE_SNPS|COLLAPSE_INDELS;

    if (!bcf_sr_add_reader(sr, vcfRef.c_str())) {
      cerr << "ERROR: Could not open " << vcfRef << " for reading: missing file or tabix index?"
	   << endl;
      exit(1);
    }
    if (!bcf_sr_add_reader(sr, vcfTarget.c_str())) {
      cerr << "ERROR: Could not open " << vcfTarget << " for reading: missing file or tabix index?"
	   << endl;
      exit(1);
    }

    bcf_hdr_t *ref_hdr = bcf_sr_get_header(sr, 0);
    bcf_hdr_t *tgt_hdr = bcf_sr_get_header(sr, 1);
  
    // Open VCF for writing, "-" stands for standard output
    //      wbu .. uncompressed BCF
    //      wb  .. compressed BCF
    //      wz  .. compressed VCF
    //      w   .. uncompressed VCF
    htsFile *out = hts_open(tmpFile.c_str(), writeMode.c_str());

    // Print the VCF header
    bcf_hdr_write(out, tgt_hdr);

    Nref = bcf_hdr_nsamples(ref_hdr);
    Ntarget = bcf_hdr_nsamples(tgt_hdr);

    // Read target sample IDs
    targetIDs.resize(Ntarget);
    for (uint i = 0; i < Ntarget; i++)
      targetIDs[i] = tgt_hdr->samples[i];

    cout << endl;
    cout << "Reference samples: Nref = " << Nref << endl;
    cout << "Target samples: Ntarget = " << Ntarget << endl;

    M = 0;
    uint MtargetOnly = 0, MrefOnly = 0, MmultiAllelic = 0, Mmonomorphic = 0;
    uint MwithMissingRef = 0, MwithUnphasedRef = 0, MnotInRegion = 0, MnotOnChrom = 0;
    uint MrefAltError = 0, numRefAltSwaps = 0;
    uint64 GmissingRef = 0, GunphasedRef = 0, GmissingTarget = 0;
    uint w = 521288629; // fast rng: Marsaglia's MWC

    int mref_gt = 0, *ref_gt = NULL;
    int mtgt_gt = 0, *tgt_gt = NULL;
    int prev_rid = -1; // chromosome BCF id and human-readable numeric id
    while ( bcf_sr_next_line(sr) )
      {
	bcf1_t *ref = bcf_sr_get_line(sr, 0);
	bcf1_t *tgt = bcf_sr_get_line(sr, 1);
	if ( !ref ) {
	  //fprintf(stderr, "onlyT .. %s:%d\n", bcf_seqname(tgt_hdr, tgt), tgt->pos+1);
	  MtargetOnly++;
	  continue;
	}
	if ( !tgt ) {
	  //fprintf(stderr, "onlyR .. %s:%d\n", bcf_seqname(ref_hdr, ref), ref->pos+1);
	  MrefOnly++;
	  continue;
	}
	//fprintf(stderr, "match .. %s:%d\n", bcf_seqname(ref_hdr, ref), ref->pos+1);

	// filter out multi-allelic and monomorphic markers
	int ntgt_gt = bcf_get_genotypes(tgt_hdr, tgt, &tgt_gt, &mtgt_gt);
	if (tgt->n_allele > 2) {
	  MmultiAllelic++;
	  continue;
	}
	if (tgt->n_allele < 2) {
	  Mmonomorphic++;
	  continue;
	}

	bool refAltSwap = false;

        if (allowRefAltSwap) { // perform further error-checking
	  if (tgt->n_allele != 2 || ref->n_allele != 2) {
	    MrefAltError++;
	    continue;
	  }
	  bcf_unpack(tgt, BCF_UN_STR); // unpack thru ALT
	  bcf_unpack(ref, BCF_UN_STR); // unpack thru ALT
	  /*
	  printf("tgt REF=%s, ALT=%s   ref REF=%s, ALT=%s\n", tgt->d.allele[0], tgt->d.allele[1],
		 ref->d.allele[0], ref->d.allele[1]);
	  */
	  if (strcmp(tgt->d.allele[0], ref->d.allele[0]) == 0 &&
	      strcmp(tgt->d.allele[1], ref->d.allele[1]) == 0) {
	    refAltSwap = false;
	  }
	  else if (strcmp(tgt->d.allele[0], ref->d.allele[1]) == 0 &&
		   strcmp(tgt->d.allele[1], ref->d.allele[0]) == 0) {
	    refAltSwap = true;
	    numRefAltSwaps++;
	  }
	  else {
	    MrefAltError++;
	    continue;	    
	  }
	}

    // Check the chromosome: if region was requested (chrom is set), synced
    // reader already positioned us in the right region. Otherwise, we process
    // only the first chromosome in the file and quit
    if ( prev_rid<0 ) 
    { 
        prev_rid = tgt->rid; 
        if ( !chrom )   // learn the human readable id
        {
            sscanf(bcf_hdr_id2name(tgt_hdr, tgt->rid), "%d", &chrom);
            if (!(chrom >= 1 && chrom <= 22)) {
                cerr << "ERROR: Invalid chromosome number: " << bcf_hdr_id2name(tgt_hdr, tgt->rid)
                    << endl;
                exit(1);
            }
        }
    }
    if ( prev_rid!=tgt->rid ) break;

	M++; // SNP passes checks

	// append chromosome number and base pair coordinate to chrBps
	chrBps.push_back(make_pair(chrom, tgt->pos+1));

	// process reference haplotypes: append 2*Nref entries (0/1 pairs) to hapsRef[]
	// check for missing/unphased ref genos (missing -> REF allele; unphased -> random phase)
	int nref_gt = bcf_get_genotypes(ref_hdr, ref, &ref_gt, &mref_gt);
	int numMissing, numUnphased;
	process_ref_genotypes(Nref, nref_gt, ref_gt, refAltSwap, hapsRef, numMissing, numUnphased,
			      w);
	if (numMissing) MwithMissingRef++;
	if (numUnphased) MwithUnphasedRef++;
	GmissingRef += numMissing;
	GunphasedRef += numUnphased;

	// process target genotypes: append Ntarget entries (0/1/2/9) to genosTarget[]
	process_target_genotypes(Ntarget, ntgt_gt, tgt_gt, genosTarget, numMissing);
	GmissingTarget += numMissing;

	// print the record
	bcf_write(out, tgt_hdr, tgt);
      }

    bcf_sr_destroy(sr);
    hts_close(out);    
    free(ref_gt);
    free(tgt_gt);

    cout << "SNPs to analyze: M = " << M << " SNPs in both target and reference" << endl;
    if (numRefAltSwaps)
      cerr << "--> WARNING: REF/ALT were swapped in " << numRefAltSwaps << " of these SNPs <--"
	   << endl;
    cout << endl;
    cout << "SNPs ignored: " << MtargetOnly << " SNPs in target but not reference" << endl;
    if (MtargetOnly > M/10U)
      cerr << "              --> WARNING: Check REF/ALT agreement between target and ref <--"
	   << endl;
    cout << "              " << MrefOnly << " SNPs in reference but not target" << endl;
    if (MnotOnChrom)
      cout << "              " << MnotOnChrom << " SNPs not in specified chrom" << endl;
    if (MnotInRegion)
      cout << "              " << MnotInRegion << " SNPs not in selected region (+ flanks)"
	   << endl;
    cout << "              " << MmultiAllelic << " multi-allelic SNPs" << endl;
    cout << "              " << Mmonomorphic << " monomorphic SNPs" << endl;
    if (MrefAltError)
      cout << "              " << MrefAltError << " SNPs with REF/ALT matching errors" << endl;
    cout << endl;
    
    if (MwithMissingRef) {
      cerr << "WARNING: Reference contains missing genotypes (set to reference allele)" << endl;
      cerr << "         Fraction of sites with missing data:  "
	   << MwithMissingRef / (double) M << endl;
      cerr << "         Fraction of ref genotypes missing:    "
	   << GmissingRef / (double) M / Nref << endl;
    }
    if (MwithUnphasedRef) {
      cerr << "WARNING: Reference contains unphased genotypes (set to random phase)" << endl;
      cerr << "         Fraction of sites with unphased data: "
	   << MwithUnphasedRef / (double) M << endl;
      cerr << "         Fraction of ref genotypes unphased:   "
	   << GunphasedRef / (double) M / Nref << endl;
    }
    cout << "Missing rate in target genotypes: " << GmissingTarget / (double) M / Ntarget << endl;
    cout << endl;

    if (M <= 1U) {
      cerr << endl << "ERROR: Target and ref have too few matching SNPs (M = " << M << ")" << endl;
      exit(1);
    }

    return chrBps;
  }

  vector <double> SyncedVcfData::processMap(vector < pair <int, int> > &chrBps,
					    const string &geneticMapFile) {
    cout << "Filling in genetic map coordinates using reference file:" << endl;
    cout << "  " << geneticMapFile << endl;
    Genetics::MapInterpolater mapInterpolater(geneticMapFile);
    vector <double> cMs(chrBps.size());
    for (uint64 m = 0; m < chrBps.size(); m++)
      cMs[m] = 100 * mapInterpolater.interp(chrBps[m].first, chrBps[m].second);
    return cMs;
  }

  void SyncedVcfData::buildGenoBits(const vector <bool> &hapsRef,
				    const vector <uchar> &genosTarget, const vector <double> &cMs,
				    double cMmax) {
    const uint segMin = 16;
    vector <uint64> snpInds; vector <double> cMvec;
    vector < vector <uint64> > seg64snpInds;
    for (uint64 m = 0; m < M; m++) {
      if (cMvec.size() == 64 || (cMvec.size() >= segMin && cMs[m] > cMvec[0] + cMmax)) {
	seg64snpInds.push_back(snpInds); seg64cMvecs.push_back(cMvec);
	snpInds.clear(); cMvec.clear();
      }
      snpInds.push_back(m); cMvec.push_back(cMs[m]);
    }
    seg64snpInds.push_back(snpInds); seg64cMvecs.push_back(cMvec);
    
    Mseg64 = seg64snpInds.size();
    cout << "Number of <=(64-SNP, " << cMmax << "cM) segments: " << Mseg64 << endl;
    cout << "Average # SNPs per segment: " << M / Mseg64 << endl;

    uint64 N = Nref + Ntarget;
    genoBits = ALIGNED_MALLOC_UINT64_MASKS(Mseg64 * N);
    memset(genoBits, 0, Mseg64 * N * sizeof(genoBits[0]));

    for (uint64 m64 = 0; m64 < Mseg64; m64++) {
      for (uint64 j = 0; j < seg64snpInds[m64].size(); j++) {
        uint64 m = seg64snpInds[m64][j];
	for (uint64 n = 0; n < Nref; n++) { // store haploBits for ref haplotypes in genoBits
	  bool haps0 = hapsRef[m * 2*Nref + 2*n];
	  bool haps1 = hapsRef[m * 2*Nref + 2*n+1];
	  genoBits[m64 * N + n].is0 |= ((uint64) haps0)<<j;
	  genoBits[m64 * N + n].is2 |= ((uint64) haps1)<<j;
	}
	for (uint64 n = Nref; n < N; n++) { // set genoBits for target genotypes
	  uchar geno = genosTarget[m * Ntarget + n-Nref];
	  genoBits[m64 * N + n].is0 |= ((uint64) (geno == 0))<<j;
	  genoBits[m64 * N + n].is2 |= ((uint64) (geno == 2))<<j;
	  genoBits[m64 * N + n].is9 |= ((uint64) (geno == 9))<<j;
	}
      }
      for (uint64 n = 0; n < N; n++)
	for (uint64 j = seg64snpInds[m64].size(); j < 64; j++)
	  genoBits[m64*N+n].is9 |= 1ULL<<j;
    }
  }

  /**    
   * reads ref+target vcf data
   * writes target[isec] to tmpFile
   * fills in cM coordinates and seg64cMvecs, genoBits
   */
  SyncedVcfData::SyncedVcfData(const string &vcfRef, const string &vcfTarget, bool allowRefAltSwap,
			       int chrom, double bpStart, double bpEnd,
			       const string &geneticMapFile, double cMmax, const string &tmpFile,
			       const string &writeMode) {

    // perform synced read
    vector <bool> hapsRef;     // M*2*Nref
    vector <uchar> genosTarget; // M*Ntarget
    vector < pair <int, int> > chrBps = 
      processVcfs(vcfRef, vcfTarget, allowRefAltSwap, chrom, bpStart, bpEnd, hapsRef, genosTarget,
		  tmpFile, writeMode);

    // interpolate genetic coordinates
    vector <double> cMs = processMap(chrBps, geneticMapFile);

    uint64 physRange = 0; double cMrange = 0;
    for (uint64 m = 0; m+1 < chrBps.size(); m++)
      if (chrBps[m+1].first == chrBps[m].first) {
	physRange += chrBps[m+1].second - chrBps[m].second;
	cMrange += cMs[m+1] - cMs[m];
      }
    cout << "Physical distance range: " << physRange << " base pairs" << endl;
    cout << "Genetic distance range:  " << cMrange << " cM" << endl;
    cout << "Average # SNPs per cM:   " << (int) (M/cMrange + 0.5)
	 << "   (recommended: 50-500 SNPs/cM)" << endl;

    if (physRange == 0 || cMrange == 0) {
      cerr << "ERROR: Physical and genetic distance ranges must be positive" << endl;
      cerr << "       First SNP: chr=" << chrBps[0].first << " pos=" << chrBps[0].second
	   << " cM=" << cMs[0] << endl;
      cerr << "       Last SNP:  chr=" << chrBps.back().first << " pos=" << chrBps.back().second
	   << " cM=" << cMs.back() << endl;
      exit(1);
    }

    buildGenoBits(hapsRef, genosTarget, cMs, cMmax);
  }

  SyncedVcfData::~SyncedVcfData() {
    ALIGNED_FREE(genoBits);
  }

  uint64 SyncedVcfData::getNref(void) const { return Nref; }
  uint64 SyncedVcfData::getNtarget(void) const { return Ntarget; }
  uint64 SyncedVcfData::getMseg64(void) const { return Mseg64; }
  const uint64_masks *SyncedVcfData::getGenoBits(void) const { return genoBits; }
  vector <vector <double> > SyncedVcfData::getSeg64cMvecs(void) const { return seg64cMvecs; }
  const string &SyncedVcfData::getTargetID(int n) const { return targetIDs[n]; }

};
