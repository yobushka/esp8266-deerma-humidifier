enum humMode_t {
  unknown = -1,
  low = 1,
  medium = 2,
  high = 3,
  setpoint = 4
};

struct humidifierState_t { 
  boolean powerOn;
  
  humMode_t mode = (humMode_t)-1;
  
  int humiditySetpoint; // This is 0 when not in setpoint mode
  
  int currentHumidity = -1; 
  int currentTemperature = -1;

  boolean soundEnabled;
  boolean ledEnabled;

  boolean waterTankInstalled;
  boolean waterTankEmpty;
};
