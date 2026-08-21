
#include "clipper/cif/cif_data_io.h"
#include "clipper/core/xmap.h"

namespace coot {

   class smcif {
      // BANDICOOT v0.2 Phase 4: the COORDINATE half of this reader is on gemmi
      // and needs none of these -- gemmi::SmallStructure parses the cell, the
      // symmetry (in the measured fallback order) and the element-plus-charge
      // symbol itself, so get_cell(), get_space_group(symm_strings),
      // read_coordinates() and symbol_to_element() are gone rather than ported.
      // The gemmi types stay OUT of this header on purpose: src/ includes it,
      // and coot-utils/mmcif-document.hh is meant to remain the only
      // gemmi-aware header in the tree.
      //
      // The REFLECTION-DATA half is on gemmi too, and reads the file ONCE --
      // the cell, space group, resolution limit, HKL list and structure
      // factors all come out of one parsed document, where there used to be
      // five separate opens through five private helpers. Those helpers
      // (get_cell_for_data, get_space_group x3, get_resolution, setup_hkls)
      // are gone with them; what they did now lives in file-static functions
      // in read-sm-cif.cc, so that no gemmi type appears in this header.


      clipper::HKL_info mydata;
      clipper::Cell data_cell;
      clipper::Spacegroup data_spacegroup;
      clipper::Resolution data_resolution;

      // fill this
      clipper::HKL_data<clipper::datatypes::F_sigF<float> > my_fsigf;
      // and this (from the real and imaginary components)
      clipper::HKL_data<clipper::datatypes::F_phi<float> >  my_fphi;
      
   public:
      smcif() {};
      smcif(const std::string &file_name) {
	 read_sm_cif(file_name);
      }
      mmdb::Manager *read_sm_cif(const std::string &file_name) const;
      // return success status, true is good
      bool read_data_sm_cif(const std::string &file_name);
      // return an empty map if not possible
      clipper::Xmap<float> map() const;
      bool check_for_f_phis() const; // use sigmaa_maps if we have phis, use
                                     // sigmaa_maps_by_calc_sfs if we don't.
      // calculate maps using fcalc and phi calc in the .cif files
      // return an empty map in first if not possible, 
      std::pair<clipper::Xmap<float>, clipper::Xmap<float> > sigmaa_maps();
      // return an empty map in first if not possible
      std::pair<clipper::Xmap<float>, clipper::Xmap<float> > sigmaa_maps_by_calc_sfs(mmdb::Atom **atom_selection, int n_selected_atoms);
   };

   // NOTE (Phase 4, 2026-08-20): this used to carry one aniso row of a
   // small-molecule CIF between the reader's two loops. gemmi keeps the ADPs on
   // the site itself, so nothing constructs one any more, and nothing else in
   // the tree ever did. Kept rather than deleted only because it is a public
   // type in the coot namespace.
   class simple_sm_u {
   public:
      std::string label; // atom name
      mmdb::realtype u11, u22, u33, u12, u13, u23;
      simple_sm_u() {
	 u11 = 0;
	 u22 = 0;
	 u33 = 0;
	 u12 = 0;
	 u13 = 0;
	 u23 = 0;
      }
      simple_sm_u(const std::string label_in,
		  mmdb::realtype u11_in, mmdb::realtype u22_in, mmdb::realtype u33_in,
		  mmdb::realtype u12_in, mmdb::realtype u13_in, mmdb::realtype u23_in) {
	 label = label_in;
	 u11 = u11_in;
	 u22 = u22_in;
	 u33 = u33_in;
	 u12 = u12_in;
	 u13 = u13_in;
	 u23 = u23_in;
      } 
   };
   
}
