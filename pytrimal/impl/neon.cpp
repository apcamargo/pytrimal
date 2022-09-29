#include <arm_neon.h>
#include <climits>
#include <cstdint>

#include "Alignment/Alignment.h"
#include "Statistics/Manager.h"
#include "Statistics/Similarity.h"
#include "InternalBenchmarker.h"
#include "reportsystem.h"
#include "defines.h"
#include "utils.h"

#include "neon.h"

#define NLANES_8  sizeof(uint8x16_t) / sizeof(uint8_t)   // number of 8-bit  lanes in __m128i

static inline uint32_t vhaddq_u8(uint8x16_t a) {
#ifdef __aarch64__
    // Don't use `vaddvq_u8` because it can overflow, first add pairwise 
    // into 16-bit lanes to avoid overflows
    uint16x8_t paired = vpaddlq_u8(a);
    return vaddvq_u16(paired);    
#else
    uint64x2_t paired = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(a)));
    return vgetq_lane_u64(paired, 0) + vgetq_lane_u64(paired, 1);
#endif
}

namespace statistics {
    NEONSimilarity::NEONSimilarity(Alignment* parentAlignment): Similarity(parentAlignment) {}
    
    void NEONSimilarity::calculateMatrixIdentity() {
        // Create a timerLevel that will report times upon its destruction
        //	which means the end of the current scope.
        StartTiming("void NEONSimilarity::calculateMatrixIdentity() ");

        // We don't want to calculate the matrix identity
        // if it has been previously calculated
        if (matrixIdentity != nullptr)
            return;

        // declare indices
        int i, j, k, l;

        // Allocate memory for the matrix identity
        matrixIdentity = new float *[alig->originalNumberOfSequences];
        for (i = 0; i < alig->originalNumberOfSequences; i++) {
            matrixIdentity[i] = new float[alig->originalNumberOfSequences];
        }

        // Depending on alignment type, indetermination symbol will be one or other
        char indet = alig->getAlignmentType() & SequenceTypes::AA ? 'X' : 'N';

        // prepare constant SIMD vectors
        const uint8x16_t allindet = vdupq_n_u8(indet);
        const uint8x16_t allgap   = vdupq_n_u8('-');
        const uint8x16_t ONES     = vdupq_n_u8(1);

        // For each sequences' pair, compare identity
        for (i = 0; i < alig->originalNumberOfSequences; i++) {
            for (j = i + 1; j < alig->originalNumberOfSequences; j++) {

                const char* datai = alig->sequences[i].data();
                const char* dataj = alig->sequences[j].data();

                uint8x16_t seqi;
                uint8x16_t seqj;
                uint8x16_t eq;
                uint8x16_t gapsi;
                uint8x16_t gapsj;
                uint8x16_t len_acc = vdupq_n_u8(0);
                uint8x16_t sum_acc = vdupq_n_u8(0);

                int sum = 0;
                int length = 0;

                // run with unrolled loops of UCHAR_MAX iterations first
                for (k = 0; ((int) (k + NLANES_8*UCHAR_MAX)) < alig->originalNumberOfResidues;) {
                    // unroll the internal loop
                    for (l = 0; l < UCHAR_MAX; l++, k += NLANES_8) {
                        // load data for the sequences
                        seqi = vld1q_u8( (const uint8_t*) (&datai[k]));
                        seqj = vld1q_u8( (const uint8_t*) (&dataj[k]));
                        // find which sequence characters are gap or indet
                        gapsi = vorrq_u8(vceqq_u8(seqi, allgap), vceqq_u8(seqi, allindet));
                        gapsj = vorrq_u8(vceqq_u8(seqj, allgap), vceqq_u8(seqj, allindet));
                        // find which sequence characters are equal
                        eq    = vceqq_u8(seqi, seqj);
                        // update counters
                        sum_acc = vaddq_u8(sum_acc, vandq_u8(eq, vbicq_u8(ONES, vorrq_u8(gapsi, gapsj))));
                        len_acc = vaddq_u8(len_acc,              vbicq_u8(ONES, vandq_u8(gapsi, gapsj)));
                    }
                    // merge accumulators
                    sum    += vhaddq_u8(sum_acc);
                    length += vhaddq_u8(len_acc);
                    sum_acc = vdupq_n_u8(0);
                    len_acc = vdupq_n_u8(0);
                }

                // run remaining iterations in SIMD while possible
                for (; ((int) (k + NLANES_8)) < alig->originalNumberOfResidues; k += NLANES_8) {
                    // load data for the sequences; load is unaligned because
                    // string data is not guaranteed to be aligned.
                    seqi = vld1q_u8( (const uint8_t*) (&datai[k]));
                    seqj = vld1q_u8( (const uint8_t*) (&dataj[k]));
                    // find which sequence characters are either a gap or
                    // an indeterminate character
                    gapsi = vorrq_u8(vceqq_u8(seqi, allgap), vceqq_u8(seqi, allindet));
                    gapsj = vorrq_u8(vceqq_u8(seqj, allgap), vceqq_u8(seqj, allindet));
                    // find which sequence characters are equal
                    eq    = vceqq_u8(seqi, seqj);
                    // update counters: update sum if both sequence characters
                    // are non-gap/indeterminate and equal; update length if
                    // any of the characters is non-gappy/indeterminate.
                    sum_acc = vaddq_u8(sum_acc, vandq_u8(eq, vbicq_u8(ONES, vorrq_u8(gapsi, gapsj))));
                    len_acc = vaddq_u8(len_acc,              vbicq_u8(ONES, vandq_u8(gapsi, gapsj)));
                }

                // merge accumulators
                sum    += vhaddq_u8(sum_acc);
                length += vhaddq_u8(len_acc);

                // process the tail elements when there remain less than
                // can be fitted in a SIMD vector
                for (; k < alig->originalNumberOfResidues; k++) {
                    int gapi = (datai[k] == '-') || (datai[k] == indet);
                    int gapj = (dataj[k] == '-') || (dataj[k] == indet);
                    sum    += (!gapi) && (!gapj) && (datai[k] == dataj[k]);
                    length += (!gapi) || (!gapj);
                }

                // Calculate the value of matrix idn for columns j and i
                matrixIdentity[i][j] = matrixIdentity[j][i] = (1.0F - ((float)sum / length));
            }
        }
    }

    bool NEONSimilarity::calculateVectors(bool cutByGap) {
        // Create a timerLevel that will report times upon its destruction
        //	which means the end of the current scope.
        StartTiming("bool GenericSimilarity::calculateVectors(int *gaps) ");

        // A similarity matrix must be defined. If not, return false
        if (simMatrix == nullptr)
            return false;

        // Calculate the matrix identity in case it's not done before
        if (matrixIdentity == nullptr)
            calculateMatrixIdentity();

        // Create the variable gaps, in case we want to cut by gaps
        int *gaps = nullptr;

        // Retrieve the gaps values in case we want to set to 0 the similarity value
        // in case the gaps value for that column is bigger or equal to 0.8F
        if (cutByGap)
        {
            if (alig->Statistics->gaps == nullptr)
                alig->Statistics->calculateGapStats();
            gaps = alig->Statistics->gaps->getGapsWindow();
        }

        // Initialize the variables used
        int i, j, k;
        float num, den;

        // Depending on alignment type, indetermination symbol will be one or other
        char indet = alig->getAlignmentType() & SequenceTypes::AA ? 'X' : 'N';

        // Q temporal value
        float Q;
        // Temporal chars that will contain the residues to compare by pair.
        int numA, numB;

        // Calculate the maximum number of gaps a column can have to calculate it's
        //      similarity
        float gapThreshold = 0.8F * alig->numberOfResidues;

        // Cache pointers to matrix rows to avoid dereferencing in inner loops
        float* identityRow;
        float* distRow;

        // Create buffers to store column data
        std::vector<char> colnum = std::vector<char>(alig->originalNumberOfSequences);
        std::vector<char> colgap = std::vector<char>(alig->originalNumberOfSequences);

        // For each column calculate the Q value and the MD value using an equation
        for (i = 0; i < alig->originalNumberOfResidues; i++) {
            // Set MDK for columns with gaps values bigger or equal to 0.8F
            if (cutByGap && gaps[i] >= gapThreshold) {
                MDK[i] = 0.F;
                continue;
            }

            // Fill the column buffer with the current column and check
            // characters are well-defined with respect to the similarity matrix
            for (j = 0; j < alig->originalNumberOfSequences; j++) {
                char letter = utils::toUpper(alig->sequences[j][i]);
                if ((letter == indet) || (letter == '-')) {
                    colgap[j] = 1;
                } else {
                    colgap[j] = 0;
                    if ((letter < 'A') || (letter > 'Z')) {
                        debug.report(ErrorCode::IncorrectSymbol, new std::string[1]{std::string(1, letter)});
                        return false;
                    } else if (simMatrix->vhash[letter - 'A'] == -1) {
                        debug.report(ErrorCode::UndefinedSymbol, new std::string[1]{std::string(1, letter)});
                        return false;
                    } else {
                        colnum[j] = simMatrix->vhash[letter - 'A'];
                    }
                }
            }

            // For each AAs/Nucleotides' pair in the column we compute its distance
            for (j = 0, num = 0, den = 0; j < alig->originalNumberOfSequences; j++) {
                // We don't compute the distance if the first element is
                // a indeterminate (XN) or a gap (-) element.
                if (colgap[j]) continue;

                // Get the index of the first residue
                // and cache pointers to matrix rows
                numA = colnum[j];
                distRow = simMatrix->distMat[numA];
                identityRow = matrixIdentity[j];

                for (k = j + 1; k < alig->originalNumberOfSequences; k++) {
                    // We don't compute the distance if the second element is
                    //      a indeterminate (XN) or a gap (-) element
                    if (colgap[k]) continue;

                    // Get the index of the second residue and compute 
                    // fraction with identity value for the two pairs and
                    // its distance based on similarity matrix's value.
                    numB = colnum[k];
                    num += identityRow[k] * distRow[numB];
                    den += identityRow[k];
                }
            }

            // If we are processing a column with only one AA/nucleotide, MDK = 0
            if (den == 0)
                MDK[i] = 0;
            else
            {
                Q = num / den;
                // If the MDK value is more than 1, we normalized this value to 1.
                //      Only numbers higher than 0 yield exponents higher than 1
                //      Using this we can test if the result is going to be higher than 1.
                //      And thus, prevent calculating the exp.
                // Take in mind that the Q is negative, so we must test if Q is LESSER
                //      than one, not bigger.
                if (Q < 0)
                    MDK[i] = 1.F;
                else
                    MDK[i] = exp(-Q);
            }
        }

        for (i = 0; i < alig->originalNumberOfSequences; i++)
            delete[] matrixIdentity[i];
        delete[] matrixIdentity;
        matrixIdentity = nullptr;

        return true;
    }
}

NEONCleaner::NEONCleaner(Alignment* parent): Cleaner(parent) {
    // allocate aligned memory for faster SIMD loads
    hits_unaligned         = (uint32_t*)      malloc(sizeof(uint32_t) * alig->originalNumberOfResidues + 0xF);
    hits_u8_unaligned      = (uint8_t*)       malloc(sizeof(uint8_t)  * alig->originalNumberOfResidues + 0xF);
    skipResidues_unaligned = (unsigned char*) malloc(sizeof(char)     * alig->originalNumberOfResidues + 0xF);

    hits         = (uint32_t*)      (((uintptr_t) hits_unaligned         + 15) & (~0xF));
    hits_u8      = (uint8_t*)       (((uintptr_t) hits_u8_unaligned      + 15) & (~0xF));
    skipResidues = (unsigned char*) (((uintptr_t) skipResidues_unaligned + 15) & (~0xF));

    // create an index for residues to skip
    for(int i = 0; i < alig->originalNumberOfResidues; i++) {
        skipResidues[i] = alig->saveResidues[i] == -1 ? 0xFF : 0;
    }
}

NEONCleaner::~NEONCleaner() {
    free(hits_u8_unaligned);
    free(hits_unaligned);
    free(skipResidues_unaligned);
}

void NEONCleaner::calculateSeqIdentity() {
  // Create a timer that will report times upon its destruction
  //	which means the end of the current scope.
  StartTiming("void NEONCleaner::calculateSeqIdentity(void) ");

  // declare indices
  int i, j, k, l;

  // Depending on alignment type, indetermination symbol will be one or other
  char indet = (alig->getAlignmentType() & SequenceTypes::AA) ? 'X' : 'N';

  // prepare constant SIMD vectors
  const uint8x16_t allindet = vdupq_n_u8(indet);
  const uint8x16_t allgap   = vdupq_n_u8('-');
  const uint8x16_t ONES     = vdupq_n_u8(1);

  // Create identities matrix to store identities scores
  alig->identities = new float*[alig->originalNumberOfSequences];
  for(i = 0; i < alig->originalNumberOfSequences; i++) {
      if (alig->saveSequences[i] == -1) continue;
      alig->identities[i] = new float[alig->originalNumberOfSequences];
      alig->identities[i][i] = 0;
  }

  // For each seq, compute its identity score against the others in the MSA
  for (i = 0; i < alig->originalNumberOfSequences; i++) {
      if (alig->saveSequences[i] == -1) continue;

      // Compute identity scores for the current sequence against the rest
      for (j = i + 1; j < alig->originalNumberOfSequences; j++) {
          if (alig->saveSequences[j] == -1) continue;

          const char* datai = alig->sequences[i].data();
          const char* dataj = alig->sequences[j].data();

          uint8x16_t seqi;
          uint8x16_t seqj;
          uint8x16_t skip;
          uint8x16_t eq;
          uint8x16_t gapsi;
          uint8x16_t gapsj;
          uint8x16_t mask;
          uint8x16_t dst_acc = vdupq_n_u8(0);
          uint8x16_t hit_acc = vdupq_n_u8(0);

          int hit = 0;
          int dst = 0;

          // run with unrolled loops of UCHAR_MAX iterations first
          for (k = 0; ((int) (k + NLANES_8*UCHAR_MAX)) < alig->originalNumberOfResidues;) {
              for (l = 0; l < UCHAR_MAX; l++, k += NLANES_8) {
                  // load data for the sequences
                  seqi = vld1q_u8((const uint8_t*) (&datai[k]));
                  seqj = vld1q_u8((const uint8_t*) (&dataj[k]));
                  skip = vld1q_u8((const uint8_t*) (&skipResidues[k]));
                  eq = vceqq_u8(seqi, seqj);
                  // find which sequence characters are gap or indet
                  gapsi = vorrq_u8(vceqq_u8(seqi, allgap), vceqq_u8(seqi, allindet));
                  gapsj = vorrq_u8(vceqq_u8(seqj, allgap), vceqq_u8(seqj, allindet));
                  // find position where not both characters are gap
                  mask  = vbicq_u8(vbicq_u8(ONES, vandq_u8(gapsi, gapsj)), skip);
                  // update counters
                  dst_acc = vaddq_u8(dst_acc,                   mask);
                  hit_acc = vaddq_u8(hit_acc, vandq_u8(eq, mask));
              }
              // merge accumulators
              dst += vhaddq_u8(dst_acc);
              hit += vhaddq_u8(hit_acc);
              dst_acc = vdupq_n_u8(0);
              hit_acc = vdupq_n_u8(0);
          }

          // run remaining iterations in SIMD while possible
          for (; ((int) (k + NLANES_8)) < alig->originalNumberOfResidues; k += NLANES_8) {
              // load data for the sequences; load is unaligned because
              // string data is not guaranteed to be aligned.
              seqi = vld1q_u8((const uint8_t*) (&datai[k]));
              seqj = vld1q_u8((const uint8_t*) (&dataj[k]));
              skip = vld1q_u8((const uint8_t*) (&skipResidues[k]));
              eq = vceqq_u8(seqi, seqj);
              // find which sequence characters are either a gap or
              // an indeterminate character
              gapsi = vorrq_u8(vceqq_u8(seqi, allgap), vceqq_u8(seqi, allindet));
              gapsj = vorrq_u8(vceqq_u8(seqj, allgap), vceqq_u8(seqj, allindet));
              // find position where not both characters are gap
              mask = vbicq_u8(vbicq_u8(ONES, vandq_u8(gapsi, gapsj)), skip);
              // update counters: update dst if any of the two sequence
              // characters is non-gap/indeterminate; update length if
              // any of the characters is non-gappy/indeterminate.
              dst_acc = vaddq_u8(dst_acc,                   mask);
              hit_acc = vaddq_u8(hit_acc, vandq_u8(eq, mask));
          }

          // update counters after last loop
          hit += vhaddq_u8(hit_acc);
          dst += vhaddq_u8(dst_acc);

          // process the tail elements when there remain less than
          // can be fitted in a SIMD vector
          for (; k < alig->originalNumberOfResidues; k++) {
              int gapi = (datai[k] == indet) || (datai[k] == '-');
              int gapj = (dataj[k] == indet) || (dataj[k] == '-');
              dst += (!(gapi && gapj)) && (!skipResidues[k]);
              hit += (!(gapi && gapj)) && (!skipResidues[k]) && (datai[k] == dataj[k]);
          }

          if (dst == 0) {
              debug.report(ErrorCode::NoResidueSequences,
                  new std::string[2]
                      {
                          alig->seqsName[i],
                          alig->seqsName[j]
                      });
              alig->identities[i][j] = 0;
          } else {
              // Identity score between two sequences is the ratio of identical residues
              // by the total length (common and no-common residues) among them
              alig->identities[i][j] = (float) hit / dst;
          }

          alig->identities[j][i] = alig->identities[i][j];
      }
  }
}

bool NEONCleaner::calculateSpuriousVector(float overlap, float *spuriousVector) {
    // Create a timer that will report times upon its destruction
    //	which means the end of the current scope.
    StartTiming("bool NEONCleaner::calculateSpuriousVector(float overlap, float *spuriousVector) ");

    // abort if there is not output vector to write to
    if (spuriousVector == nullptr)
        return false;

    // compute number of sequences from overlap threshold
    uint32_t  ovrlap  = uint32_t(ceil(overlap * float(alig->originalNumberOfSequences - 1)));

    // Depending on alignment type, indetermination symbol will be one or other
    char indet = (alig->getAlignmentType() & SequenceTypes::AA) ? 'X' : 'N';

    // prepare constant SIMD vectors
    const uint8x16_t allindet  = vdupq_n_u8(indet);
    const uint8x16_t allgap    = vdupq_n_u8('-');
    const uint8x16_t ONES      = vdupq_n_u8(1);

    // for each sequence in the alignment, computes its overlap
    for (int i = 0; i < alig->originalNumberOfSequences; i++) {

        // reset hits count
        memset(&hits[0],    0, alig->originalNumberOfResidues*sizeof(uint32_t));
        memset(&hits_u8[0], 0, alig->originalNumberOfResidues*sizeof(uint8_t));

        // compare sequence to other sequences for every position
        for (int j = 0; j < alig->originalNumberOfSequences; j++) {

            // don't compare sequence to itself
            if (j == i)
                continue;

            const char* datai = alig->sequences[i].data();
            const char* dataj = alig->sequences[j].data();

            uint8x16_t seqi;
            uint8x16_t seqj;
            uint8x16_t eq;
            uint8x16_t gapsi;
            uint8x16_t gapsj;
            uint8x16_t gaps;
            uint8x16_t n;
            uint8x16_t hit;

            int k = 0;

            // run iterations in SIMD while possible
            for (; ((int) (k + NLANES_8)) <= alig->originalNumberOfResidues; k += NLANES_8) {
                // load data for the sequences
                seqi = vld1q_u8((const uint8_t*) (&datai[k]));
                seqj = vld1q_u8((const uint8_t*) (&dataj[k]));
                // find which sequence characters are gap or indet
                gapsi = vorrq_u8(vceqq_u8(seqi, allgap), vceqq_u8(seqi, allindet));
                gapsj = vorrq_u8(vceqq_u8(seqj, allgap), vceqq_u8(seqj, allindet));
                gaps  = vmvnq_u8(vorrq_u8(gapsi, gapsj));
                // find which sequence characters match
                eq = vceqq_u8(seqi, seqj);
                // find position where either not both characters are gap, or they are equal
                n = vandq_u8(vorrq_u8(eq, gaps), ONES);
                // update counters
                hit = vld1q_u8((const uint8_t*) (&hits_u8[k]));
                hit = vaddq_u8(hit, n);
                vst1q_u8((uint8_t*) (&hits_u8[k]), hit);
            }

            // process the tail elements when there remain less than
            // can be fitted in a SIMD vector
            for (; k < alig->originalNumberOfResidues; k++) {
                int nongapi = (datai[k] != indet) && (datai[k] != '-');
                int nongapj = (dataj[k] != indet) && (dataj[k] != '-');
                hits_u8[k] += ((nongapi && nongapj) || (datai[k] == dataj[k]));
            }

            // we can process up to UCHAR_MAX sequences, otherwise hits_u8[k]
            // may overflow, so every UCHAR_MAX iterations we transfer the
            // partial hit counts from `hits_u8` to `hits`
            if ((j % UCHAR_MAX) == 0) {
                for (k = 0; k < alig->originalNumberOfResidues; k++) hits[k] += hits_u8[k];
                memset(hits_u8, 0, alig->originalNumberOfResidues*sizeof(uint8_t));
            }
        }

        // update counters after last loop
        for (int k = 0; k < alig->originalNumberOfResidues; k++) hits[k] += hits_u8[k];

        // compute number of good positions in for sequence i
        uint32_t seqValue = 0;
        for (int k = 0; k < alig->originalNumberOfResidues; k++)
            if (hits[k] >= ovrlap)
                seqValue++;

        // compute overlap of current sequence as the fraction of columns
        // above overlap threshold
        spuriousVector[i] = ((float) seqValue / alig->originalNumberOfResidues);
    }

    // If there is not problem in the method, return true
    return true;
}
