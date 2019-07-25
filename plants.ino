#include <cube_spline.h>
#include <Adafruit_Sensor.h>
#include <SD.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>

/* Temperature and Humidity Sensor stuff */
#define DHTPIN 2 /* Pin */
#define DHTTYPE DHT11 /* Model */

/* Strings */
#define HEADER_FLAG "\nSTARTING A HEADER\n\n"
#define HEADER "\nTime, Light Intensity (lux), Temperature (C), Humidity, "\
                "Soil, CO2 (ppm)\n"

/* Outfile name */
#define FILENAME "CHVS526.csv"

/* Number of points used in interpolation */
#define NUM_POINTS 6

/* Struct for temperature/humidy sensor*/
DHT_Unified dht(DHTPIN, DHTTYPE);


/* Struct for keeping track of times */
typedef struct time_val time_val;
struct time_val {
  unsigned int hours;
  unsigned int minutes;
  unsigned int seconds;
};

/* Voltages and lux values for interpolation for photoresistor,
   Had to add a dummy value to make spline behave (4, 43) 
*/
float init_voltages[NUM_POINTS] = {0.1, 0.6, 2.5, 4, 4.3, 5.0}; /* x */
float init_lux[NUM_POINTS] = {0.1, 1, 10, 43, 100, 1000}; /* y */

/* S pointer points to interpolation results */
S *function;

/* start holds compile time (used as reference for calculating times */
time_val start;

/* Use this start time to calculate our start time_val */
unsigned long start_millis = millis();


/* These take up lots of space, but it's nice to use the String objects
   when formatting later on
*/
String comma = ", ";
String colon = ":"; 
String zero = "0";


void setup() {
  
  Serial.begin(9600);

  /* 10 is the critical pin for SD card shield */
  pinMode(10, OUTPUT); 
    
  /* Initialize  humidity/temperature sensor */
  pinMode(DHTPIN, OUTPUT); 

  /* Start SD and dht */
  SD.begin(10);
  dht.begin();

  /* Write a header to FILENAME */
  write_header(&start);
  
  /* Perform the cubic spline to be used on every photoresistor data point */
  function = nat_cubic_spline(init_voltages, init_lux, NUM_POINTS);
}

void loop() {
    File out; /* where we write */

    /* temp and hum values read from dht sensors */
    sensors_event_t temp;
    sensors_event_t hum;

    /* output values from sensors */
    float lux;
    float co2;
    
    int num; /* For error checking */
    
    /* Temporary variables for storing data read from sensor before 
       it's converted to meaningful units
    */
    float v_light;
    float v_soil;
    float tmp;
    
    /* For getting our current time */
    time_val cur_time;

    /* Photoresistor data */
    tmp = analogRead(A0);
    v_light = ((float)tmp)/1023 * 5;

    /* Soil data */
    tmp = analogRead(A1); 
    v_soil = ((float)tmp/1023) * 5;

    /* Co2 data */
    tmp = analogRead(A2);
    co2 = ((float)tmp/1023)*5*2000/4;
  
    /* Convert v_light to lux with interpolation */
    num = evaluate(function, v_light, &lux);

    /* Check the error values (-1 means v is too small, -2 means too big) */
    if (num == -1) {
      lux = 0;
    }

    if (num == -2) {
      lux = -1;
    }
    
    
    /* Humidity and Temperature data in degrees C and relative % */
    dht.temperature().getEvent(&temp); 
    dht.humidity().getEvent(&hum);
    

    /* Open FILENAME for writing */
    /* file names must use 8.3 convention 
    (8 character name, 3 character extension)
    */
    out = SD.open(FILENAME, FILE_WRITE); 

   if (out) {

        /* Build current time in format: HH:MM:SS*/
        get_cur_time(&cur_time, &start, start_millis);

        String my_time = build_time_string(cur_time);
        
        
        /* Build string for one call to print */
        String s = my_time + comma + lux + comma + (int) temp.temperature + 
                comma + hum.relative_humidity + comma + v_soil + 
                comma + co2;        
       
        Serial.println(s);
  
        /* Write to file and close it */
        out.println(s);
        out.close();
        
       }
    else {
      /* If the file couldn't be opened, try to reinitialize for next data 
         point
      */
      SD.begin(10);
    }
  
    /* Take a data point every 2 seconds */
    delay(2000);
}
void write_header(time_val *s_time) {
/* This function writes a header to FILENAME at startup */
/* Format is:
    HEADER_FLAG
    Compile date and compile time
    HEADER
*/

  File out;
  char cur_time[] = __TIME__; /* Compile time */
  char date[] = __DATE__; /* Compile date */
  char *tmp;
  char flag[] = HEADER_FLAG;
  char header[] = HEADER;
 
  
  

  out = SD.open(FILENAME, FILE_WRITE);
 
  if (out) {
    print_header_serial(); /* print header to serial port */
    delay(50); /* Need to wait or the write gets skipped ... idk why */
    out.write(flag);
    out.close(); /* Need to close or next write get skipped ... */
    
    /* Write compile date and time */
    out = SD.open(FILENAME, FILE_WRITE);
    out.write("Compile Date: ");
    out.write(date);
    out.write(", Compile time: ");
    out.write(cur_time);

    /* write HEADER */
    out.write(header);

    out.close();
    
    
    /* Set start time_val */
    s_time->hours = (int) strtol(cur_time, &tmp, 10);
    s_time->minutes = (int) strtol(tmp+1, &tmp, 10);
    s_time->seconds = (int) strtol(tmp+1, NULL, 10);
    
  }
  else {
    /* If SD card can't be opened, bail */
    Serial.println("Couldn't open file");
    delay(50);
    exit(1);
  }
 

}

void get_cur_time(time_val *cur_time, time_val *start, 
        unsigned long start_millis) {

/* This function uses the current time in milliseconds, the start 
   time_val and the start time in milliseconds to build a current time_val
   struct.
   There's a bug that increments the hour when it hits the compile time ie:
   If the program was compiled at 5:23:17, when the time hits 5:59:59, the
   next time is 6:00:00 as expected, but when the time hits 6:23:17, the hour
   value is incremented so the next time is 7:23:18. This is bad.
*/
  unsigned long my_time;
  int seconds;
  int minutes;
  int hours;
  int tmp;
  my_time = millis() - start_millis;
  my_time = my_time / 1000; /* Convert to seconds */


  /* Time since start */
  seconds = my_time%60; 
  minutes = my_time/60; 
  hours = my_time/(60*60); 

  /* Put that in terms of start: */
  cur_time->seconds = (seconds + start->seconds) % 60;
  tmp = ((minutes + start->minutes) + (seconds + start->seconds)/60);
  cur_time->minutes = tmp%60;
  cur_time->hours = ((hours + start->hours) + (tmp)/60)%24;
  
}
String build_time_string(time_val cur_time) {
  String sec;
  String my_min;
  String hour; 
  /* This function just takes a time_val struct and returns it in the format:
     HH:MM:SS
  */

   if (cur_time.seconds < 10) {
          sec = zero + cur_time.seconds;
        }
        else {
          sec = cur_time.seconds;
        }
        
        if (cur_time.minutes < 10) {
          my_min = zero + cur_time.minutes;
        }
        else {
          my_min = cur_time.minutes;
        }
        
        if (cur_time.hours < 10) {
          hour = zero + cur_time.hours;
        }
        else {
          hour = cur_time.hours;
        }

        return hour + colon + my_min + colon + sec;
  
}

void print_header_serial() {
/* Print header to Serial port */

  Serial.println();
  Serial.println(HEADER_FLAG);
  Serial.print("Compile Date: ");
  Serial.print(__DATE__);
  Serial.print(", Compile time: ");
  Serial.println(__TIME__);
  Serial.print(HEADER);
  
}
