/*
 * $Id$
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * GROwing Monsters And Cloning Shrimps
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include "sysstuff.h"
#include "princ.h"
#include "futil.h"
#include "statutil.h"
#include "vec.h"
#include "smalloc.h"
#include "typedefs.h"
#include "names.h"
#include "gmx_fatal.h"
#include "macros.h"
#include "rdgroup.h"
#include "symtab.h"
#include "readir.h"
#include "string.h"
#include "mdatoms.h"
#include "pull.h"

#define MAX_PULL_GROUPS 21

static char pulldim[STRLEN];

static void string2dvec(char buf[], dvec nums)
{
  if (sscanf(buf,"%lf%lf%lf",&nums[0],&nums[1],&nums[2]) != 3)
    gmx_fatal(FARGS,"Expected three numbers at input line %s",buf);
}

static void init_pullgrp(t_pullgrp *pg,char *wbuf,
			 bool bRef,int eGeom,char *s_vec)
{
  double d;
  int    n,m;
  dvec   vec;

  pg->nweight = 0;
  while (sscanf(wbuf,"%lf %n",&d,&n) == 1) {
    if (pg->nweight % 100 == 0) {
      srenew(pg->weight,pg->nweight+100);
    }
    pg->weight[pg->nweight++] = d;
    wbuf += n;
  }
  if (!bRef) {
    if (eGeom == epullgDIST) {
      clear_dvec(vec);
    } else {
      string2dvec(s_vec,vec);
      if (eGeom == epullgDIR || eGeom == epullgCYL || 
	  (eGeom == epullgPOS && dnorm(vec) != 0))
	/* Normalize the direction vector */
	dsvmul(1/dnorm(vec),vec,vec);
    }
    for(m=0; m<DIM; m++)
      pg->vec[m] = vec[m];
  }
}

char **read_pullparams(int *ninp_p,t_inpfile **inp_p,
		       t_pull *pull,bool *bStart,int *nerror_p) 
{
  int  ninp,nerror,i,nchar,ndim,nscan,m;
  t_inpfile *inp;
  char *tmp,**grpbuf;
  char dummy[STRLEN],buf[STRLEN],init[STRLEN];
  char *init_def1="0.0",*init_def3="0.0 0.0 0.0";
  char wbuf[MAX_PULL_GROUPS][STRLEN],
    bf[MAX_PULL_GROUPS][STRLEN],
    pos[MAX_PULL_GROUPS][STRLEN];
  char VecTemp[MAX_PULL_GROUPS][STRLEN], InitTemp[MAX_PULL_GROUPS][STRLEN];
  dvec vec;

  t_pullgrp *pgrp;

  ninp   = *ninp_p;
  inp    = *inp_p;
  nerror = *nerror_p;

  /* read pull parameters */
  CTYPE("Pull geometry: distance, direction, cylinder or position");
  EETYPE("pull_geometry",   pull->eGeom, epullg_names, &nerror, TRUE);
  CTYPE("Select components for the pull vector. default: Y Y Y");
  STYPE("pull_dim",         pulldim, "Y Y Y");
  CTYPE("Cylinder radius for dynamic reaction force groups (nm)");
  RTYPE("pull_r1",          pull->cyl_r1, 1.0);
  CTYPE("Switch from r1 to r0 in case of dynamic reaction force");
  RTYPE("pull_r0",          pull->cyl_r0, 1.5);
  RTYPE("pull_constr_tol",  pull->constr_tol, 1E-6);
  EETYPE("pull_start",      *bStart, yesno_names, &nerror, TRUE);
  ITYPE("pull_nstxout",     pull->nstxout, 10);
  ITYPE("pull_nstfout",     pull->nstfout,  1);
  CTYPE("Number of pull groups");
  ITYPE("pull_ngroups",     pull->ngrp,1);

  if (pull->cyl_r1 > pull->cyl_r0) {
    fprintf(stderr,"ERROR: pull_r1 > pull_r0\n");
    nerror++;
  }

  if (pull->ngrp < 1 || pull->ngrp > MAX_PULL_GROUPS-1)
    gmx_fatal(FARGS,"pull_ngroups should be between 1 and %d",
	      MAX_PULL_GROUPS-1);
  
  snew(pull->grp,pull->ngrp+1);

  if (pull->eGeom == epullgPOS)
    ndim = 3;
  else
    ndim = 1;

  /* pull group options */
  CTYPE("Group name, weight (default all 1), vector, init, rate (nm/ps), kJ/(mol*nm^2)");
  /* Read the pull groups */
  snew(grpbuf,pull->ngrp+1);
  for(i=0; i<pull->ngrp+1; i++) {
    pgrp = &pull->grp[i];
    snew(grpbuf[i],STRLEN);
    sprintf(buf,"pull_group%d",i);
    STYPE(buf,              grpbuf[i], "");
    sprintf(buf,"pull_weights%d",i);
    STYPE(buf,              wbuf[i], "");
    sprintf(buf,"pull_pbcatom%d",i);
    ITYPE(buf,              pgrp->pbcatom, 0);
    if (i > 0) {
      sprintf(buf,"pull_vec%d",i);
      STYPE(buf,              VecTemp[i], "0.0 0.0 0.0");
      sprintf(buf,"pull_init%d",i);
      STYPE(buf,              init, ndim==1 ? init_def1 : init_def3);
      clear_dvec(vec);
      nscan = sscanf(init,"%lf %lf %lf",&vec[0],&vec[1],&vec[2]);
      if (nscan != ndim) {
	fprintf(stderr,"ERROR: %s should have %d components\n",buf,ndim);
	nerror++;
      }
      for(m=0; m<DIM; m++)
	pgrp->init[m] = vec[m];
      sprintf(buf,"pull_rate%d",i);
      RTYPE(buf,              pgrp->rate, 0.0);
      sprintf(buf,"pull_k%d",i);
      RTYPE(buf,              pgrp->k, 0.0);
    }
  }
  
  /* Initialize the pull groups */
  for(i=0; i<pull->ngrp+1; i++) {
    init_pullgrp(&pull->grp[i],wbuf[i],i==0,pull->eGeom,VecTemp[i]);
  }
  
  *ninp_p   = ninp;
  *inp_p    = inp;
  *nerror_p = nerror;

  return grpbuf;
}

void make_pull_groups(t_pull *pull,char **pgnames,t_block *grps,char **gnames)
{
  int  d,nchar,g,ig=-1,i;
  char *ptr,pulldim1[STRLEN];
  t_pullgrp *pgrp;

  if (pull->eGeom == epullgDIST || pull->eGeom == epullgCYL) {
    pull->dim[0] = 1;
    pull->dim[1] = 1;
    pull->dim[2] = 1;
  } else {
    ptr = pulldim;
    i = 0;
    for(d=0; d<DIM; d++) {
      if (sscanf(ptr,"%s%n",pulldim1,&nchar) != 1)
	gmx_fatal(FARGS,"Less than 3 pull dimensions given in pull_dim: '%s'",
		  pulldim);
      
      if (strncasecmp(pulldim1,"N",1) == 0) {
	pull->dim[d] = 0;
      } else if (strncasecmp(pulldim1,"Y",1) == 0) {
	pull->dim[d] = 1;
	i++;
      } else {
	gmx_fatal(FARGS,"Please use Y(ES) or N(O) for pull_dim only (not %s)",
		  pulldim1);
      }
      ptr += nchar;
    }
    if (i == 0)
      gmx_fatal(FARGS,"All entries in pull_dim are N");
  }

  for(g=0; g<pull->ngrp+1; g++) {
    pgrp = &pull->grp[g];
    if (g == 0 && strcmp(pgnames[g],"") == 0) {
      pgrp->nat = 0;
    } else {
      ig = search_string(pgnames[g],grps->nr,gnames);
      pgrp->nat = grps->index[ig+1] - grps->index[ig];
    }
    if (pgrp->nat > 0) {
      fprintf(stderr,"Pull group %d '%s' has %d atoms\n",
	      g,pgnames[g],pgrp->nat);
      snew(pgrp->ind,pgrp->nat);
      for(i=0; i<pgrp->nat; i++)
	pgrp->ind[i] = grps->a[grps->index[ig]+i];

      if (pull->eGeom == epullgCYL && g == 0 && pgrp->nweight > 0)
	gmx_fatal(FARGS,"Weights are not supported for the reference group with cylinder pulling");
      if (pgrp->nweight > 0 && pgrp->nweight != pgrp->nat)
	gmx_fatal(FARGS,"Number of weights (%d) for pull group %d '%s' does not match the number of atoms (%d)",
		  pgrp->nweight,g,pgnames[g],pgrp->nat);

      if (pgrp->nat == 1) {
	/* No pbc is required for this group */
	pgrp->pbcatom = -1;
      } else {
	if (pgrp->pbcatom > 0)
	  pgrp->pbcatom -= 1;
	else
	  pgrp->pbcatom = pgrp->ind[(pgrp->nat-1)/2];
      }

      if ((pull->eGeom == epullgDIR || pull->eGeom == epullgCYL) &&
	  g > 0 && norm2(pgrp->vec) == 0)
	gmx_fatal(FARGS,"pull_vec%d can not be zero with geometry %s",
		  g,EPULLGEOM(pull->eGeom));
      if ((pull->eGeom == epullgPOS) && pgrp->rate != 0 &&
	  g > 0 && norm2(pgrp->vec) == 0)
	gmx_fatal(FARGS,"pull_vec%d can not be zero with geometry %s and non-zero rate",
		  g,EPULLGEOM(pull->eGeom));
      if (pull->eGeom == epullgCYL &&
	  (pgrp->vec[XX] != 0 || pgrp->vec[YY] != 0))
	gmx_fatal(FARGS,"With geometry %s pull_vec%d should have x and y components set to zero",EPULLGEOM(pull->eGeom),g);
    } else {
      if (g == 0) {
	if (pull->eGeom == epullgCYL)
	  gmx_fatal(FARGS,"Absolute reference groups are not supported with geometry %s",EPULLGEOM(pull->eGeom));
      } else {
	gmx_fatal(FARGS,"Pull group %d '%s' is empty",g,pgnames[g]);
      }
      pgrp->pbcatom = -1;
    }
  }
}

void set_pull_init(t_inputrec *ir,t_atoms *atoms,rvec *x,matrix box,
		   bool bStart)
{
  t_mdatoms *md;
  t_pull    *pull;
  t_pullgrp *pgrp;
  int       ndim,g,m;
  double    tinvrate;
  rvec      init;
  dvec      dr,dev;

  init_pull(NULL,ir,0,NULL,atoms,NULL,FALSE);
  md = init_mdatoms(NULL,atoms,ir->efep);
  atoms2md(atoms,ir,0,0,NULL,0,atoms->nr,md);
  if (ir->efep)
    update_mdatoms(md,ir->init_lambda);
  
  pull = ir->pull;
  if (pull->eGeom == epullgPOS)
    ndim = 3;
  else
    ndim = 1;

  pull_calc_coms(NULL,pull,md,x,NULL,box);

  fprintf(stderr,"Pull group  natoms  pbc atom  distance at start     reference at t=0\n");
  for(g=0; g<pull->ngrp+1; g++) {
    pgrp = &pull->grp[g];
    fprintf(stderr,"%8d  %8d  %8d ",g,pgrp->nat,pgrp->pbcatom+1);
    copy_rvec(pgrp->init,init);
    clear_rvec(pgrp->init);
    if (g > 0) {
      if (pgrp->rate == 0)
	tinvrate = 0;
      else
	tinvrate = (ir->init_t + ir->init_step*ir->delta_t)/pgrp->rate;
      get_pullgrp_distance(pull,g,box,0,dr,dev);
      for(m=0; m<DIM; m++) {
	if (m < ndim)
	  fprintf(stderr," %6.3f",dev[m]);
	else
	  fprintf(stderr,"       ");
      }
      fprintf(stderr," ");
      for(m=0; m<DIM; m++) {
	if (bStart)
	  pgrp->init[m] = init[m] + dev[m]
	    - tinvrate*(pull->eGeom==epullgPOS ? pgrp->vec[m] : 1);
	else
	  pgrp->init[m] = init[m];
	if (m < ndim)
	  fprintf(stderr," %6.3f",pgrp->init[m]);
	else
	  fprintf(stderr,"       ");
      }
    }
    fprintf(stderr,"\n");
  }
}
