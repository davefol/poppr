/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# This software was authored by Zhian N. Kamvar and Javier F. Tabima, graduate
# students at Oregon State University; and Dr. Nik Grünwald, an employee of
# USDA-ARS.
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for educational, research and non-profit purposes, without fee,
# and without a written agreement is hereby granted, provided that the statement
# above is incorporated into the material, giving appropriate attribution to the
# authors.
#
# Permission to incorporate this software into commercial products may be
# obtained by contacting USDA ARS and OREGON STATE UNIVERSITY Office for
# Commercialization and Corporate Development.
#
# The software program and documentation are supplied "as is", without any
# accompanying services from the USDA or the University. USDA ARS or the
# University do not warrant that the operation of the program will be
# uninterrupted or error-free. The end-user understands that the program was
# developed for research purposes and is advised not to rely exclusively on the
# program for any reason.
#
# IN NO EVENT SHALL USDA ARS OR OREGON STATE UNIVERSITY BE LIABLE TO ANY PARTY
# FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
# LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
# EVEN IF THE OREGON STATE UNIVERSITY HAS BEEN ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE. USDA ARS OR OREGON STATE UNIVERSITY SPECIFICALLY DISCLAIMS ANY
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND ANY STATUTORY
# WARRANTY OF NON-INFRINGEMENT. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
# BASIS, AND USDA ARS AND OREGON STATE UNIVERSITY HAVE NO OBLIGATIONS TO PROVIDE
# MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#include <stdio.h>
#include <Rinternals.h>
#include <Rdefines.h>
#include <R.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>

// Assumptions:
//  All genotypes have the same number of SNPs available.
//  All SNPs are diploid.

// TODO: Add support of non-diploids
// TODO: Write more R functions that make use of the data this spits out

struct zygosity
{
  char c1;  // A 32 bit fragment of one chromosome  
  char c2;  // The corresponding fragment from the outer chromosome

  char cx;  // Heterozygous sites are indicated by 1's
  char ca;  // Homozygous dominant sites are indicated by 1's
  char cn;  // Homozygous recessive sites are indicated by 1's
};

struct locus
{
  int d;  // Number of dominant alleles found at this locus across genotypes
  int r;  // Number of recessive alleles found at this locus across genotypes
  int h;  // Number of genotypes that are heterozygous at this locus
  int n;  // Number of genotypes that contributed data to this struct  

};

SEXP bitwise_distance(SEXP genlight, SEXP missing, SEXP requested_threads);
void fill_Pgen(double *pgen, struct locus *loc, SEXP genlight);
void fill_loci(struct locus *loc, SEXP genlight);
void fill_zygosity(struct zygosity *ind);
char get_similarity_set(struct zygosity *ind1, struct zygosity *ind2);
int get_zeros(char sim_set);
int get_difference(struct zygosity *z1, struct zygosity *z2);

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates the pairwise differences between samples in a genlight object. The
distances represent the number of sites between individuals which differ in 
zygosity.

Input: A genlight object containing samples of diploids.
Output: A distance matrix representing the number of differences in zygosity
        between each sample.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
SEXP bitwise_distance(SEXP genlight, SEXP missing, SEXP requested_threads)
{
  SEXP R_out;
  SEXP R_gen_symbol;
  SEXP R_chr_symbol;  
  SEXP R_nap_symbol;
  SEXP R_gen;
  int num_gens;
  SEXP R_chr1_1;
  SEXP R_chr1_2;
  SEXP R_chr2_1;
  SEXP R_chr2_2;
  SEXP R_nap1;
  SEXP R_nap2;
  int nap1_length;
  int nap2_length;
  int chr_length;
  int next_missing_index_i;
  int next_missing_index_j;
  int next_missing_i;
  int next_missing_j;
  int missing_match;
  int num_threads;
  char mask;
  struct zygosity set_1;
  struct zygosity set_2;
  int i;
  int j;
  int k;

  int** distance_matrix;
  int cur_distance;
  char tmp_sim_set;

  
  PROTECT(R_gen_symbol = install("gen")); // Used for accessing the named elements of the genlight object
  PROTECT(R_chr_symbol = install("snp"));
  PROTECT(R_nap_symbol = install("NA.posi"));  

  // This will be a LIST of type LIST:RAW
  R_gen = getAttrib(genlight, R_gen_symbol);
  num_gens = XLENGTH(R_gen);

  PROTECT(R_out = allocVector(INTSXP, num_gens*num_gens));
  distance_matrix = R_Calloc(num_gens,int*);
  for(i = 0; i < num_gens; i++)
  {
    distance_matrix[i] = R_Calloc(num_gens,int);
  }

  // Set the number of threads to be used in each omp parallel region
  if(INTEGER(requested_threads)[0] == 0)
  {
    num_threads = omp_get_max_threads();
  }
  else
  {
    num_threads = INTEGER(requested_threads)[0];
  }
  omp_set_num_threads(num_threads);


  next_missing_index_i = 0;
  next_missing_index_j = 0;
  next_missing_i = 0;
  next_missing_j = 0;
  mask = 0;
  tmp_sim_set = 0;
  nap1_length = 0;
  nap2_length = 0;
  chr_length = 0;
  missing_match = asLogical(missing);

  // Loop through every genotype 
  for(i = 0; i < num_gens; i++)
  {
    R_chr1_1 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,i),R_chr_symbol),0); // Chromosome 1
    R_chr1_2 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,i),R_chr_symbol),1); // Chromosome 2
    chr_length = XLENGTH(R_chr1_1);
    R_nap1 = getAttrib(VECTOR_ELT(R_gen,i),R_nap_symbol); // Vector of the indices of missing values 
    nap1_length = XLENGTH(R_nap1);
    // Loop through every other genotype
    #pragma omp parallel for \
      private(j,cur_distance,R_chr2_1,R_chr2_2,R_nap2,next_missing_index_j,next_missing_j,next_missing_index_i,next_missing_i,\
              set_1,set_2,tmp_sim_set, k, mask, nap2_length) \
      shared(R_nap1, nap1_length, i, distance_matrix)
    for(j = 0; j < i; j++)
    {
      cur_distance = 0;
      // These will be arrays of type RAW
      R_chr2_1 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,j),R_chr_symbol),0); // Chromosome 1
      R_chr2_2 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,j),R_chr_symbol),1); // Chromosome 2
      R_nap2 = getAttrib(VECTOR_ELT(R_gen,j),R_nap_symbol); // Vector of the indices of missing values
      nap2_length = XLENGTH(R_nap2);
      next_missing_index_j = 0; // Next set of chromosomes start back at index 0
      next_missing_j = (int)INTEGER(R_nap2)[next_missing_index_j] - 1; //Compensate for Rs 1 based indexing
      next_missing_index_i = 0;
      next_missing_i = (int)INTEGER(R_nap1)[next_missing_index_i] - 1;
      // Finally, loop through all the chunks of SNPs for these genotypes
      for(k = 0; k < chr_length; k++) 
      {
        set_1.c1 = (char)RAW(R_chr1_1)[k];
        set_1.c2 = (char)RAW(R_chr1_2)[k];
        fill_zygosity(&set_1);

        set_2.c1 = (char)RAW(R_chr2_1)[k];
        set_2.c2 = (char)RAW(R_chr2_2)[k];
        fill_zygosity(&set_2);

        tmp_sim_set = get_similarity_set(&set_1,&set_2);

        // Check for missing values and force them to match
        while(next_missing_index_i < nap1_length && next_missing_i < (k+1)*8 && next_missing_i >= (k*8) )
        {
          // Handle missing bit in both chromosomes and both samples with mask
          mask = 1 << (next_missing_i%8); 
          if(missing_match)
          {
            tmp_sim_set |= mask; // Force the missing bit to match
          }
          else
          {
            tmp_sim_set &= ~mask; // Force the missing bit to not match
          }
          next_missing_index_i++; 
          next_missing_i = (int)INTEGER(R_nap1)[next_missing_index_i] - 1;
        }       
        // Repeat for j
        while(next_missing_index_j < nap2_length && next_missing_j < (k+1)*8 && next_missing_j >= (k*8))
        {
          // Handle missing bit in both chromosomes and both samples with mask
          mask = 1 << (next_missing_j%8);
          if(missing_match)
          {
            tmp_sim_set |= mask; // Force the missing bit to match
          }
          else
          {
            tmp_sim_set &= ~mask; // Force the missing bit to not match
          }
          next_missing_index_j++;
          next_missing_j = (int)INTEGER(R_nap2)[next_missing_index_j] - 1;
        }       

        // Add the distance from this word into the total between these two genotypes
        cur_distance += get_zeros(tmp_sim_set);
      }
      // Store the distance between these two genotypes in the distance matrix
      distance_matrix[i][j] = cur_distance;
      distance_matrix[j][i] = cur_distance;
    } // End parallel
  } 

  // Fill the output matrix
  for(i = 0; i < num_gens; i++)
  {
    for(j = 0; j < num_gens; j++)
    {
      INTEGER(R_out)[i*num_gens+j] = distance_matrix[i][j];
    }
  }

  for(i = 0; i < num_gens; i++)
  {
    R_Free(distance_matrix[i]);
  }
  R_Free(distance_matrix);
  UNPROTECT(4); 
  return R_out;
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Fills an array of doubles with the Pgen value associated with each individual
found in the genlight object. These values represent the probability of each
individual having been produced via random mating of the population, as estimated
by the samples present in the genlight object

Input: A pointer to an array of doubles to be filled.
        NOTE: This array MUST have a length equal to the number of genotypes found
        in each in the genlight object.
       A pointer to an array of struct locus objects that hold the values need
       A genlight object from which the individual genotypes can be obtained.
Output: None. Fills in the array of doubles with the Pgen value of each individual
        genotype in the genlight object.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void fill_Pgen(double *pgen, struct locus *loc, SEXP genlight)
{

  // Note that this may need to be done logarithmically to avoid underflowing the doubles

  // Allocate memory for the struct array, als
  // Call fill_loci to get allelic frequency information
  // for each genotype
    // set product to 1
    // set heterozygotes to 0
    // for each locus group
      // for each bit in locus group
        // if genotype is heterozygous at this site
          // add one to heterzygotes
          // product *= dominants * recessives / n + -1 * dominants * recessives * ( 1 - h / (2*dominants*recessives))
        // else the genotype is homozygous dominant
          // product *= dominants*dominants / n + (dominants/n) * (1 - dominants/n) * ( 1 - h / (2*dominants*recessives))
        // else the genotype is homozygous recessive
          // product *= recessives*recessives / n + (recessives/n) * (1 - recessives/n) * ( 1 - h / (2*dominants*recessives))
    // Copy product into pgen[genotype]


  // Free memory allocated for the structs

}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Fills an array of struct locus objects based on allelic frequencies found in
the provided genlight object.

Input: A pointer to an array of locus objects to be filled.
        NOTE: This array MUST have a length equal to the number of loci found
        in each genotype in the genlight object.
       A genlight object from which the alleles and loci can be gathered.
Output: None. Fills in the allelic frequencies and other information found in
        each locus struct.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void fill_loci(struct locus *loc, SEXP genlight)
{ 
  // ~Pseudo code~
  // for each genotype in genlight
    // for each locus group in genotype
      // zyg.c1 = locus[0]
      // zyg.c2 = locus[1]
      // fill_zygosity(&zyg) // Find zygosity at each site in locus
      // for each bit in locus group 
        // if not missing
          // // Exactly one of the next three lines will add 1 (or 2) to its respective counter
            // Add 1 to h if this is a heterozygous site
          // loc[locus*8+bit].h += (zyg.cx & (1<<bit)) >> bit // Or is that 1 << 7-bit ?
            // Add 1 to dominant if this is heterozygous or 2 if this is homozygous dominant
          // loc[locus*8+bit].d += ((zyg.cx & (1<<bit)) >> bit) + 2 * (zyg.ca & (1<<bit)) 
            // Add 1 to recessive if this is heterozygous or 2 if this is homozygous recessive
          // loc[locus*8+bit].r += ((zyg.cx & (1<<bit)) >> bit) + 2 * (zyg.cn & (1<<bit)) 
            // Add 1 to the number of contributing genotypes regardless of what else was added
          // loc[locus*8+bit].n += 1

  struct zygosity zyg;
  SEXP R_gen_symbol;
  SEXP R_chr_symbol;  
  SEXP R_nap_symbol;
  SEXP R_gen;
  int num_gens;
  SEXP R_chr1_1;
  SEXP R_chr1_2;
  SEXP R_nap1;
  int nap1_length;
  int chr_length;
  int next_missing_index_i;
  int next_missing_i;
  int i;
  int bit;
  int locus;

  PROTECT(R_gen_symbol = install("gen")); // Used for accessing the named elements of the genlight object
  PROTECT(R_chr_symbol = install("snp"));
  PROTECT(R_nap_symbol = install("NA.posi"));  

  // This will be a LIST of type LIST:RAW
  R_gen = getAttrib(genlight, R_gen_symbol);
  num_gens = XLENGTH(R_gen);
  
  next_missing_index_i = 0;
  next_missing_i = 0;
  nap1_length = 0;
  chr_length = 0;

  // Loop through every genotype 
  for(i = 0; i < num_gens; i++)
  {
    R_chr1_1 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,i),R_chr_symbol),0); // Chromosome 1
    R_chr1_2 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,i),R_chr_symbol),1); // Chromosome 2
    chr_length = XLENGTH(R_chr1_1);
    R_nap1 = getAttrib(VECTOR_ELT(R_gen,i),R_nap_symbol); // Vector of the indices of missing values 
    nap1_length = XLENGTH(R_nap1);
    next_missing_index_i = 0;
    next_missing_i = (int)INTEGER(R_nap1)[next_missing_index_i] - 1;
    // Loop through all the chunks of SNPs in this genotype
    for(locus = 0; locus < chr_length; locus++) 
    {
      zyg.c1 = (char)RAW(R_chr1_1)[locus];
      zyg.c2 = (char)RAW(R_chr1_2)[locus];
      fill_zygosity(&zyg);
      
      // TODO: Find a way to parallelize either here or the locus loop.
      //       Unfortunately, next_missing_i complicates this.
      for(bit = 0; bit < 8; bit++)
      {
        // if not missing
        if(next_missing_i != locus*8+bit)
        { 
          // Exactly one of the next three lines will add 1 (or 2) to its respective counter
          loc[locus*8+bit].h += (zyg.cx & (1<<bit)) >> bit; // Or is that 1 << 7-bit ?
          loc[locus*8+bit].d += ((zyg.cx & (1<<bit)) >> bit) + 2 * (zyg.ca & (1<<bit)); 
          loc[locus*8+bit].r += ((zyg.cx & (1<<bit)) >> bit) + 2 * (zyg.cn & (1<<bit)); 
          loc[locus*8+bit].n += 1;
        }
        else
        {
          if(next_missing_index_i < nap1_length-1)
          {
            next_missing_index_i++;
            next_missing_i = (int)INTEGER(R_nap1)[next_missing_index_i] - 1;
          }
        }
      }
    }
  } 

  UNPROTECT(3);
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates the zygosity at each location of a given section. The zygosity struct
must have c1 and c2 filled before calling this function.

Input: A zygosity struct pointer with c1 and c2 filled for the given section.
Output: None. Fills the cx, ca, and cn values in the provided struct to indicate
        heterozygous, homozygous dominant, and homozygous recessive sites respectively,
        where 1's in each string represent the presence of that zygosity and 0's
        represent a different zygosity.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void fill_zygosity(struct zygosity *ind)
{
  ind->cx = ind->c1 ^ ind->c2;  // Bitwise Exclusive OR provides 1's only at heterozygous sites
  ind->ca = ind->c1 & ind->c2;  // Bitwise AND provides 1's only at homozygous dominant sites
  ind->cn = ~(ind->c1 | ind->c2); // Bitwise NOR provides 1's only at homozygous recessive sites
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Finds the locations at which two samples have differing zygosity.

Input: Two zygosity struct pointers with c1 and c2 filled, each representing the
       same section from two samples.
Output: A char representing a binary string of differences between the
        two samples in the given section. 0's represent a difference in zygosity
        at that location and 1's represent matching zygosity.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
char get_similarity_set(struct zygosity *ind1, struct zygosity *ind2)
{

  char s;   // The similarity values to be returned
  char sx;  // 1's wherever both are heterozygous
  char sa;  // 1's wherever both are homozygous dominant
  char sn;  // 1's wherever both are homozygous recessive

  sx = ind1->cx & ind2->cx;
  sa = ind1->ca & ind2->ca;
  sn = ind1->cn & ind2->cn;

  s = sx | sa | sn; // 1's wherever both individuals share the same zygosity  

  return s;
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates the number of zeros in a binary string. Used by get_difference to
find the number of differences between two samples in a given section.

Input: A char representing a binary string of differences between samples
       where 1's are matches and 0's are differences.
Output: The number of zeros in the argument value, representing the number of 
        differences between the two samples in the location used to generate
        the sim_set.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int get_zeros(char sim_set)
{
  int zeros = 0;
  int tmp_set = sim_set;
  int digits = 8; //sizeof(char);
  int i;

  for(i = 0; i < digits; i++)
  {
    if(tmp_set%2 == 0)
    {
      zeros++;
    }
    tmp_set = tmp_set >> 1; // Drop the rightmost digit and shift the others over 1
  }

  return zeros;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Counts the number of differences between two partially filled zygosity structs.
c1 and c2 must be filled in both structs prior to calling this function.

Input: Two zygosity struct pointers representing the two sections to be compared.
       c1 and c2 must be filled in both structs.
Output: The number of locations in the given section that have differing zygosity
        between the two samples.
        cx, ca, and cn will be filled in both structs as a byproduct of this function.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int get_difference(struct zygosity *z1, struct zygosity *z2)
{
  int dif = 0;
  fill_zygosity(z1);
  fill_zygosity(z2);
  dif = get_zeros(get_similarity_set(z1,z2));

  return dif;
}


