#include <data.hpp>

Data::Data() {
	dt = 0.0;
  dim = -1;
  npulses = 0;
  tstart = 0.0;
  tstop = 1.0e14;
}

Data::Data(std::vector<std::string> data_name_, double tstop_, int dim_, int npulses_) {
  data_name = data_name_;
  dim = dim_;
  npulses = npulses_;
  tstop = tstop_;

  // Set outer dimension of the data to number of control pulses. Inner dimension will be filled in the subclasses.
  data.resize(npulses);
}

Data::~Data() {
  for (int ipulse=0; ipulse<data.size(); ipulse++){
    for (int j=0; j<data[ipulse].size(); j++){
      VecDestroy(&(data[ipulse][j]));
    }
    data[ipulse].clear();
  }
  data.clear();
}

Vec Data::getData(double time, int pulse_num){
  
  if (tstart <= time && time <= tstop) {  // if time is within the data domain
    double remainder = std::remainder(time - tstart, dt);
    if (abs(remainder) < 1e-10) {        // if data exists at this time
      int dataID = round((time - tstart)/dt);
      return data[pulse_num][dataID]; 
    } else return NULL;
  } else return NULL;
}


void Data::writeExpectedEnergy(const char* filename, int pulse_num){

  /* Open file  */
  FILE *file_c;
  file_c = fopen(filename, "w");

  /* Iterate over time points, compute expected energy and write to file. */
  for (int i=0; i<getNData(); i++){
    double time = tstart + i*dt;

    Vec x = getData(time, pulse_num);
    if (x != NULL) {
      double val = expectedEnergy(x, LindbladType::BOTH);
      fprintf(file_c, "% 1.8f   % 1.14e   \n", time, val);
    }
  }
  fclose(file_c);
  printf("File written: %s\n", filename);
}

SyntheticQuandaryData::SyntheticQuandaryData(std::vector<std::string> data_name, double data_tstop, int dim) : Data(data_name, data_tstop, dim, 1) {

  /* Load training data */
  loadData(&tstart, &tstop, &dt);
}

SyntheticQuandaryData::~SyntheticQuandaryData() {

}

void SyntheticQuandaryData::loadData(double* tstart, double* tstop, double* dt){

  // Open files 
  std::ifstream infile_re;
  std::ifstream infile_im;
  infile_re.open(data_name[0], std::ifstream::in);
  infile_im.open(data_name[1], std::ifstream::in);
  if(infile_re.fail() || infile_im.fail() ) {// checks to see if file opended 
      std::cout << "\n ERROR loading learning data file " << data_name[0] << std::endl;
      exit(1);
  } else {
    std::cout<< "Loading synthetic data from " << data_name[0] << ", " << data_name[1] << std::endl;
  }

  // Iterate over each line in the files
  int count = 0;
  double time_re, time_im, time_prev;
  while (infile_re >> time_re) 
  {
    // Figure out time and dt
    if (count == 0) *tstart = time_re;
    if (count == 1) *dt = time_re - time_im; // Note: since 'time_re' is read in the 'while' statement, it will have value from the 2nd row here, whereas time_im still has the value from the first row, hence dt = re - im
    infile_im >> time_im; // Read in time for the im file (it's already done for re in the while statement!)
    // printf("time_re = %1.8f, time_im = %1.8f\n", time_re, time_im);
    assert(fabs(time_re - time_im) < 1e-12);

    // printf("Loading data at Time %1.8f\n", time_re);
    // Break if exceeding the requested time domain length
    if (time_re > *tstop)  {
      // printf("Stopping data at %1.8f > %1.8f \n", time_re, tstop);
      break;
    }

    // Now iterate over the remaining columns and store values.
    Vec state;
    VecCreate(PETSC_COMM_WORLD, &state);
    VecSetSizes(state, PETSC_DECIDE, 2*dim);
    VecSetFromOptions(state);
    double val_re, val_im;
    for (int i=0; i<dim; i++) { // Other elements are the state (re and im) at this time
      infile_re >> val_re;
      infile_im >> val_im;
      VecSetValue(state, getIndexReal(i), val_re, INSERT_VALUES);
      VecSetValue(state, getIndexImag(i), val_im, INSERT_VALUES);
    }
    VecAssemblyBegin(state);
    VecAssemblyEnd(state);

    // Store the state
    data[0].push_back(state);  // Here, only one pulse
    count+=1;
    time_prev = time_re;
  }

  /* Update the final time stamp */
  *tstop = std::min(time_prev, *tstop);

  // Close files
	infile_re.close();
	infile_im.close();

  // // TEST what was loaded
  // printf("\nDATA POINTS:\n");
  // for (int i=0; i<data.size(); i++){
  //   VecView(data[i], NULL);
  // }
  // printf("END DATA POINTS.\n\n");
}


Tant2levelData::Tant2levelData(std::vector<std::string> data_name, double data_tstop, int dim, int npulses) : Data(data_name, data_tstop, dim, npulses){
  // Currently only for 2level data. 
  assert(dim == 4);

  /* Load training data, this also sets first and last time stamp as well as data sampling step size */
  loadData(&tstart, &tstop, &dt);
}

Tant2levelData::~Tant2levelData(){

}

void Tant2levelData::loadData(double* tstart, double* tstop, double* dt){

  /* Data format: First row is header following rows are formated as follows: 
  *   <nshots> <time> <pulse_num> <rho_lie_ij> for ij=1,2 <rho_lie_phys_ij> for ij=12 
  *    int     double   int       (val_re+val_imj)              (val_re+val_imj)
  */

  // Open file
  std::ifstream infile;
  infile.open(data_name[0], std::ifstream::in);
  if(infile.fail() ) {// checks to see if file opended 
      std::cout << "\n ERROR loading learning data file " << data_name[0] << std::endl;
      exit(1);
  } else {
    std::cout<< "Loading Tant Device data from " << data_name[0] << std::endl;
  }

  // Skip first line, its just the header.
  std::string tmp; 
  for (int i=0; i< 300; i++){
    infile >> tmp;
    if (tmp.compare("rho_lie_phys_11") == 0) {
      break;
    }
  }

  // Iterate over lines
  int count = 0;
  double time, time_prev;
  int pulse_num;
  std::string strval;

  while (infile >> nshots) 
  {
    infile >> time;      // 2rd column;
    infile >> pulse_num; // 3th column;

    /* Only read the first <npulses> trajectories */
    if (pulse_num >= npulses) {
      break;
    }

    // Figure out first time point and sampling time-step
    if (count == 0) *tstart = time;
    if (count == 1) *dt = time - time_prev; 

    // printf("tstart = %1.8f, dt=%1.8f\n", tstart, data_dt);
    // printf("Loading data at Time %1.8f\n", time);

    // Break if exceeding the requested time domain length 
    if (time > *tstop)  {
      if (npulses == 1) {
        break; 
      } else { // Skip to next pulse number. TODO: TEST THIS!! Probably wrong...
        while (infile >> strval) {
          if (strval.compare(std::to_string(pulse_num+1)) == 0) {
            break;
          }
        }
      }
    }
    
    // Now skip to the corrected physical LIE data (skip next <dim> columns)
    for (int i=0; i < dim; i++){
      infile >> strval;
    }
    
    // Allocate the state 
    Vec state;
    VecCreate(PETSC_COMM_WORLD, &state);
    VecSetSizes(state, PETSC_DECIDE, 2*dim);
    VecSetFromOptions(state);
 
    // Iterate over the remaining columns and store values.
    int id=0;
    for (int i=0; i<dim; i++) {
      // Read string and remove the brackets around (val_re+val_imj) as well as the trailing "j" character
      infile >> strval;
      std::string stripped = strval.substr(1, strval.size()-3);  
      // Extract real and imaginary parts, with correct sign.
      std::stringstream stream(stripped); 
      double val_re, val_im;
      char sign;
      while (stream >> val_re >> sign >> val_im) {
        if (sign == '-') val_im = -val_im;
        // std::cout<< " -> Got val = " << val_re << " " << val_im << " j" << std::endl;
      }
      VecSetValue(state, getIndexReal(id), val_re, INSERT_VALUES);
      VecSetValue(state, getIndexImag(id), val_im, INSERT_VALUES);
      id++;
    }
    VecAssemblyBegin(state);
    VecAssemblyEnd(state);

    // Store the state
    data[pulse_num].push_back(state);
    count+=1;
    time_prev = time;
  }

  /* Update the final time stamp */
  *tstop = std::min(time_prev, *tstop);

  // Close files
	infile.close();

  // // TEST what was loaded
  // printf("\nDATA POINTS:\n");
  // for (int ipulse=0; ipulse<data.size(); ipulse++){
  //   printf("PULSE NUMBER %d\n", ipulse);
  //   for (int j=0; j<data[ipulse].size(); j++){
  //     VecView(data[ipulse][j], NULL);
  //   }
  // }
  // printf("END DATA POINTS.\n\n");
  // exit(1);
}