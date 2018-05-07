#include <pybind11/pybind11.h>
#include "dta_api.h"


#include "dta_gradient_utls.h"

#include <unordered_map>

namespace py = pybind11;

int run_dta(std::string folder) {
  printf("Current working directory is......\n");
  std::cout << folder << std::endl;

  MNM_Dta *test_dta = new MNM_Dta(folder);
  test_dta -> build_from_files();
  printf("Hooking......\n");
  test_dta -> hook_up_node_and_link();
  // printf("Checking......\n");
  // test_dta -> is_ok();
  test_dta -> loading(false);


  delete test_dta;

  return 0;
}


Dta_Api::Dta_Api()
{
  m_dta = NULL;
  m_link_vec = std::vector<MNM_Dlink*>();
  m_path_vec = std::vector<MNM_Path*>();
  m_path_set = std::set<MNM_Path*>(); 
  m_ID_path_mapping = std::unordered_map<TInt, MNM_Path*>();
}

Dta_Api::~Dta_Api()
{
  if (m_dta != NULL){
    delete m_dta;
  }
  m_link_vec.clear();
  m_path_vec.clear();
  
}

int Dta_Api::initialize(std::string folder)
{
  m_dta = new MNM_Dta(folder);
  m_dta -> build_from_files();
  m_dta -> hook_up_node_and_link();
  m_dta -> is_ok();
  if (MNM_Routing_Fixed *_routing = dynamic_cast<MNM_Routing_Fixed *>(m_dta -> m_routing)){
    MNM::get_ID_path_mapping(m_ID_path_mapping, _routing -> m_path_table);
  }
  return 0;
}

int Dta_Api::run_once()
{
  return 0;
}

int Dta_Api::install_cc()
{
  for (size_t i = 0; i<m_link_vec.size(); ++i){
    m_link_vec[i] -> install_cumulative_curve();
  }
  return 0;
}

int Dta_Api::install_cc_tree()
{
  for (size_t i = 0; i<m_link_vec.size(); ++i){
    m_link_vec[i] -> install_cumulative_curve_tree();
  }
  return 0;
}

int Dta_Api::run_whole()
{

  m_dta -> loading(false);
  return 0;
}

int Dta_Api::get_cur_loading_interval()
{
  return m_dta -> m_current_loading_interval();
}

int Dta_Api::register_links(py::array_t<int> links)
{
  if (m_link_vec.size() > 0){
    printf("Warning, Dta_Api::register_links, link exists\n");
    m_link_vec.clear();
  }
  auto links_buf = links.request();
  if (links_buf.ndim != 1){
    throw std::runtime_error("Number of dimensions must be one");
  }
  int *links_ptr = (int *) links_buf.ptr;
  MNM_Dlink *_link;
  for (int i = 0; i < links_buf.shape[0]; i++){
    _link = m_dta -> m_link_factory -> get_link(TInt(links_ptr[i]));
    if(std::find(m_link_vec.begin(), m_link_vec.end(), _link) != m_link_vec.end()) {
      throw std::runtime_error("Error, Dta_Api::register_links, link exists");
    } 
    else {
      m_link_vec.push_back(_link);
    }
  }
  return 0;
}

int Dta_Api::register_paths(py::array_t<int> paths)
{
  if (m_link_vec.size() > 0){
    printf("Warning, Dta_Api::register_paths, path exists\n");
    m_path_vec.clear();
    m_path_set.clear();
  }
  auto paths_buf = paths.request();
  if (paths_buf.ndim != 1){
    throw std::runtime_error("register_paths: Number of dimensions must be one");
  }
  int *paths_ptr = (int *) paths_buf.ptr; 
  TInt _path_ID;
  for (int i = 0; i < paths_buf.shape[0]; i++){
    _path_ID = TInt(paths_ptr[i]);
    printf("registering path %d, %d\n", _path_ID(), (int)m_ID_path_mapping.size());
    if (m_ID_path_mapping.find(_path_ID) == m_ID_path_mapping.end()){
      throw std::runtime_error("register_paths: No such path");
    }
    else {
      m_path_vec.push_back(m_ID_path_mapping[_path_ID]);
    }
  }
  m_path_set = std::set<MNM_Path*> (m_path_vec.begin(), m_path_vec.end());
  return 0;
}

py::array_t<double> Dta_Api::get_link_inflow(py::array_t<int>start_intervals, py::array_t<int>end_intervals)
{
  auto start_buf = start_intervals.request();
  auto end_buf = end_intervals.request();
  if (start_buf.ndim != 1 || end_buf.ndim != 1){
    throw std::runtime_error("Error, Dta_Api::get_link_inflow, input dismension mismatch");
  }
  if (start_buf.shape[0] != end_buf.shape[0]){
    throw std::runtime_error("Error, Dta_Api::get_link_inflow, input length mismatch");
  }
  int l = start_buf.shape[0];
  int new_shape [2] = { (int) m_link_vec.size(), l};
  auto result = py::array_t<double>(new_shape);
  auto result_buf = result.request();
  double *result_prt = (double *) result_buf.ptr;
  int *start_prt = (int *) start_buf.ptr;
  int *end_prt = (int *) end_buf.ptr;
  for (int t = 0; t < l; ++t){
    for (size_t i = 0; i<m_link_vec.size(); ++i){
      if (end_prt[t] < start_prt[t]){
        throw std::runtime_error("Error, Dta_Api::get_link_inflow, end time smaller than start time");
      }
      if (end_prt[t] > get_cur_loading_interval()){
        throw std::runtime_error("Error, Dta_Api::get_link_inflow, loaded data not enough");
      }

      result_prt[i * l + t] = MNM_DTA_GRADIENT::get_link_inflow(m_link_vec[i], TFlt(start_prt[t]), TFlt(end_prt[t]))();
      // printf("i %d, t %d, %f\n", i, t, result_prt[i * l + t]);
    }
  }
  return result;
}

py::array_t<double> Dta_Api::get_link_tt(py::array_t<int>start_intervals)
{
  auto start_buf = start_intervals.request();
  if (start_buf.ndim != 1){
    throw std::runtime_error("Error, Dta_Api::get_link_tt, input dismension mismatch");
  }
  int l = start_buf.shape[0];
  int new_shape [2] = { (int) m_link_vec.size(), l}; 

  auto result = py::array_t<double>(new_shape);
  auto result_buf = result.request();
  double *result_prt = (double *) result_buf.ptr;
  int *start_prt = (int *) start_buf.ptr;
  for (int t = 0; t < l; ++t){
    for (size_t i = 0; i<m_link_vec.size(); ++i){
      if (start_prt[i] > get_cur_loading_interval()){
        throw std::runtime_error("Error, Dta_Api::get_link_tt, loaded data not enough");
      }
      result_prt[i * l + t] = MNM_DTA_GRADIENT::get_travel_time(m_link_vec[i], TFlt(start_prt[t]))();
    }
  }
  return result;
}

py::array_t<double> Dta_Api::get_link_in_cc(int link_ID)
{
  if (m_dta -> m_link_factory -> get_link(TInt(link_ID)) -> m_N_in == NULL){
    throw std::runtime_error("Error, Dta_Api::get_link_in_cc, cc not installed");
  }
  std::deque<std::pair<TFlt, TFlt>> _record = m_dta -> m_link_factory -> get_link(TInt(link_ID)) -> m_N_in -> m_recorder;
  int new_shape [2] = { (int) _record.size(), 2}; 
  auto result = py::array_t<double>(new_shape);
  auto result_buf = result.request();
  double *result_prt = (double *) result_buf.ptr;
  for (size_t i=0; i< _record.size(); ++i){
    result_prt[i * 2 ] = _record[i].first();
    result_prt[i * 2 + 1 ] =  _record[i].second();
  }
  return result;
}


py::array_t<double> Dta_Api::get_link_out_cc(int link_ID)
{
  if (m_dta -> m_link_factory -> get_link(TInt(link_ID)) -> m_N_out == NULL){
    throw std::runtime_error("Error, Dta_Api::get_link_out_cc, cc not installed");
  }
  std::deque<std::pair<TFlt, TFlt>> _record = m_dta -> m_link_factory -> get_link(TInt(link_ID)) -> m_N_out -> m_recorder;
  int new_shape [2] = { (int) _record.size(), 2}; 
  auto result = py::array_t<double>(new_shape);
  auto result_buf = result.request();
  double *result_prt = (double *) result_buf.ptr;
  for (size_t i=0; i< _record.size(); ++i){
    result_prt[i * 2 ] = _record[i].first();
    result_prt[i * 2 + 1 ] =  _record[i].second();
  }
  return result;
}


py::array_t<double> Dta_Api::get_dar_matrix(py::array_t<int>start_intervals, py::array_t<int>end_intervals)
{
  auto start_buf = start_intervals.request();
  auto end_buf = end_intervals.request();
  if (start_buf.ndim != 1 || end_buf.ndim != 1){
    throw std::runtime_error("Error, Dta_Api::get_link_inflow, input dismension mismatch");
  }
  if (start_buf.shape[0] != end_buf.shape[0]){
    throw std::runtime_error("Error, Dta_Api::get_link_inflow, input length mismatch");
  }
  int l = start_buf.shape[0];
  int *start_prt = (int *) start_buf.ptr;
  int *end_prt = (int *) end_buf.ptr;
  std::vector<dar_record*> _record = std::vector<dar_record*>();
  // for (size_t i = 0; i<m_link_vec.size(); ++i){
  //   m_link_vec[i] -> m_N_in_tree -> print_out();
  // }
  for (int t = 0; t < l; ++t){
    for (size_t i = 0; i<m_link_vec.size(); ++i){
      if (end_prt[t] < start_prt[t]){
        throw std::runtime_error("Error, Dta_Api::get_dar_matrix, end time smaller than start time");
      }
      if (end_prt[t] > get_cur_loading_interval()){
        throw std::runtime_error("Error, Dta_Api::get_dar_matrix, loaded data not enough");
      }
        MNM_DTA_GRADIENT::add_dar_records(
                      _record, m_link_vec[i], m_path_set, TFlt(start_prt[t]), TFlt(end_prt[t]));
    }
  }
  // path_ID, assign_time, link_ID, start_int, flow
  int new_shape [2] = { (int) _record.size(), 5}; 
  auto result = py::array_t<double>(new_shape);
  auto result_buf = result.request();
  double *result_prt = (double *) result_buf.ptr;
  dar_record* tmp_record;
  for (size_t i = 0; i < _record.size(); ++i){
    tmp_record = _record[i];
    result_prt[i * 5 + 0] = (double) tmp_record -> path_ID();
    result_prt[i * 5 + 1] = (double) tmp_record -> assign_int();
    result_prt[i * 5 + 2] = (double) tmp_record -> link_ID();
    result_prt[i * 5 + 3] = (double) tmp_record -> link_start_int();
    result_prt[i * 5 + 4] = tmp_record -> flow();
  }
  for (size_t i = 0; i < _record.size(); ++i){
    delete _record[i];
  }
  _record.clear();
  return result;
}




PYBIND11_MODULE(MNMAPI, m) {
    m.doc() = R"pbdoc(
        Pybind11 example plugin
        -----------------------

        .. currentmodule:: cmake_example

        .. autosummary::
           :toctree: _generate

           run_dta
    )pbdoc";

    m.def("run_dta", &run_dta, R"pbdoc(
        Run MAC-POSTS dta model

        Some other explanation about the add function.
    )pbdoc");

    // m.def("subtract", [](int i, int j) { return i - j; }, R"pbdoc(
    //     Subtract two numbers

    //     Some other explanation about the subtract function.
    // )pbdoc");
    py::class_<Dta_Api> (m, "dta_api")
            .def(py::init<>())
            .def("initialize", &Dta_Api::initialize)
            .def("run_whole", &Dta_Api::run_whole)
            .def("install_cc", &Dta_Api::install_cc)
            .def("install_cc_tree", &Dta_Api::install_cc_tree)
            .def("get_cur_loading_interval", &Dta_Api::get_cur_loading_interval)
            .def("register_links", &Dta_Api::register_links)
            .def("register_paths", &Dta_Api::register_paths)
            .def("get_link_tt", &Dta_Api::get_link_tt)
            .def("get_link_inflow", &Dta_Api::get_link_inflow)
            .def("get_link_in_cc", &Dta_Api::get_link_in_cc)
            .def("get_link_out_cc", &Dta_Api::get_link_out_cc)
            .def("get_dar_matrix", &Dta_Api::get_dar_matrix);

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}
