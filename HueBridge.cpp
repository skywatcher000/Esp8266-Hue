
#include "HueBridge.h"

  	HueBridge::HueBridge(ESP8266WebServer * HTTP, uint8_t lights, uint8_t groups, HueBridge::HueHandlerFunctionNEW fn): 
  		_HTTP(HTTP),
  		_LightCount(lights),
  		_GroupCount(groups),
//  		_Handler(fn),
  		_HandlerNEW(fn),
  		_returnJSON(true), 
//  		_RunningGroupCount(2)
  		_nextfreegroup(0)
		
  	{
  		if (_GroupCount < 2) _GroupCount = 2; // minimum required

  	 	initHUE(_LightCount, _GroupCount); // creates the data structures...


  	}

  	HueBridge::~HueBridge()   	{

		if (Lights) delete[] Lights;
		if (Groups) delete[] Groups;

		Lights = NULL;
		Groups = NULL; // Must check these.... 

		_HTTP->onNotFound(NULL); 
  	}

void HueBridge::Begin() {

  		_macString = String(WiFi.macAddress());  // toDo turn to 4 byte array..
  		_ipString  = StringIPaddress(WiFi.localIP()); 
  		_netmaskString  = StringIPaddress(WiFi.subnetMask()); 
  		_gatewayString = StringIPaddress(WiFi.gatewayIP()); 

  	 	initSSDP();
  		
  		_HTTP->onNotFound(std::bind(&HueBridge::HandleWebRequest, this)); //  register the onNotFound for Webserver to HueBridge
  		_HTTP->serveStatic("/hue/lights.conf", SPIFFS , "/lights.conf"); 
  		_HTTP->serveStatic("/hue/groups.conf", SPIFFS , "/groups.conf"); 
  		_HTTP->serveStatic("/hue/test.conf", SPIFFS , "/test.conf"); 
  		_HTTP->on("/hue/format", [this]() {
				SPIFFS.format();
				_HTTP->send ( 200, "text/plain", "DONE" );
			} );

 		Serial.println(F("Philips Hue Started...."));
 		Serial.print(F("Size Lights = "));
 		Serial.println(sizeof(HueLight) * _LightCount);
 		Serial.print(F("Size Groups = "));
 		Serial.println(sizeof(HueGroup) * _GroupCount);

 		 SPIFFS.begin();
/*------------------------------------------------------------------------------------------------

									Import saved Lights from SPIFFS

------------------------------------------------------------------------------------------------*/
  	 	File LightsFile = SPIFFS.open("/lights.conf", "r");
  		
  		if (!LightsFile)
  			{
    			Serial.println(F("Failed to open lights.conf")); 
  			} else {
  				Serial.println(F("Opened lights.conf"));
  				Serial.print(F("CONFIG FILE CONTENTS----------------------"));
  				Serial.println();

  				uint8_t light[sizeof(HueLight)];
  				size_t size = LightsFile.size();
				uint8_t counter = 0; 

				for(int i=0; i<size; i+=sizeof(HueLight)){
  				
  					for(int j=0;j<sizeof(HueLight);j++){
    					light[j] = LightsFile.read();
  					}
  				
  				HueLight *thelight = (HueLight *)light;
    			HueLight *currentlight = &Lights[counter];
    			memcpy (currentlight , thelight , sizeof(HueLight));

  				Serial.printf( "Saved Light:%3u, %15s, State = %u, HSB(%5u,%3u,%3u) \n", 
      			counter, thelight->Name, thelight->State, thelight->hsb.H, thelight->hsb.S, thelight->hsb.B);
				
				counter++; 
				}

  				LightsFile.close();
  			}
/*------------------------------------------------------------------------------------------------

									Import saved Groups from SPIFFS


------------------------------------------------------------------------------------------------*/
  	 	File GroupsFile = SPIFFS.open("/groups.conf", "r");
  		
  		if (!GroupsFile)
  			{
    			Serial.println(F("Failed to open groups.conf")); 
  			} else {

  				Serial.println(F("Opened groups.conf"));
  				Serial.print(F("CONFIG FILE CONTENTS----------------------"));
  				Serial.println();

  				uint8_t group[sizeof(HueGroup)];
  				size_t size = GroupsFile.size();
				uint8_t counter = 0; 

				for(int i=0; i<size; i+=sizeof(HueGroup)){
  				
  					for(int j=0;j<sizeof(HueGroup);j++){
    					group[j] = GroupsFile.read();
  					}
  				
  					HueGroup *thegroup = (HueGroup *)group; // Cast retrieved data as HueGroup... 
    				HueGroup *currentgroup = &Groups[counter]; // pointer to actual array
    				memcpy (currentgroup , thegroup , sizeof(HueGroup));
  					Serial.printf( "Saved Group:%3u, %15s, inuse=%u, State=%u, HSB(%5u,%3u,%3u) \n", 
      				counter, thegroup->Name, thegroup->Inuse, thegroup->State, thegroup->Hue, thegroup->Sat, thegroup->Bri);
					counter++; 
				}

  				GroupsFile.close();
  			}


  			_nextfreegroup = find_nextfreegroup(); 

}

void HueBridge::initHUE(uint8_t Lightcount, uint8_t Groupcount) {

		if (Lights) delete[] Lights;
		if (Groups) delete[] Groups;

 		Lights = new HueLight[Lightcount];
 		Groups = new HueGroup[Groupcount];

 		for (uint8_t i = 0; i < Lightcount; i++) {
    		HueLight* currentlight = &Lights[i];
    		sprintf(currentlight->Name, "Hue Light %u", i+1); 
 		}

		for (uint8_t i = 0; i < Groupcount; i++) {
  			HueGroup *g = &Groups[i];
  			if ( i == 0 || i == 1) g->Inuse = true; // sets group 0 and 1 to always be in use... 
  			sprintf(g->Name, "Group %u", i); 
  			g->LightsCount = 0 ; 

      		for (uint8_t i = 0; i < MaxLightMembersPerGroup; i++) {
        		//uint8_t randomlight = random(1,Lightcount + 1 );
        		g->LightMembers[i] = 0; 
      		}

 		}

}


void HueBridge::HandleWebRequest() {

    //------------------------------------------------------
    //                    Initial web request handles  .... 
    //------------------------------------------------------
     HTTPMethod RequestMethod = _HTTP->method(); 
     long startime = millis();

     // Serial.print("Uri: ");
     // Serial.println(_HTTP->uri());

  //    if (_lastrequest > 0) {

  //    	long timer = millis() - _lastrequest;
  //    	Serial.print("TSLR = ");
  //    	Serial.print(timer);
  //    	Serial.print("  :");
		// }
		// _lastrequest = millis();
    /////////////////////////////////


  	//------------------------------------------------------
    //                    Extract User    
    //------------------------------------------------------

    //Hue_Commands Command;  // holds command issued by client 


    if ( _HTTP->uri() != "/api" && _HTTP->uri().charAt(4) == '/' && _HTTP->uri() != "/api/config"  ) {
      user = _HTTP->uri().substring(5, _HTTP->uri().length()); 
      if (user.indexOf("/") > 0) {
        user = user.substring(0, user.indexOf("/"));
      } 
    }

    //Serial.print("Session user = ");
    //Serial.println(user);

    isAuthorized = true; 

    if (!isAuthorized) return;  // exit if none authorised user... toDO... 

    //------------------------------------------------------
    //                    Determine command   
    //------------------------------------------------------

//     /api/JPnfsdoKSVacEA0f/lights/8   --> renames light  
    //Serial.print("  ");
    size_t sent = 0; 

    if        ( _HTTP->uri() == "/description.xml") {

        Handle_Description(); 
        return; 

    } else if ( _HTTP->uri() == "/api"  ) {
        Serial.println("CREATE_USER - toDO"); 
        //Command = CREATE_USER;
        _HTTP->send(404);
        return; 
    } else if ( _HTTP->uri().endsWith("/config") ) {

        //Command = GET_CONFIG; 
        sent = printer.Send( _HTTP->client() , 200, "text/json", std::bind(&HueBridge::Send_Config_Callback, this) ); // Send_Config_Callback
        return; 
    } else if (  _HTTP->uri() == "/api/" + user ) {
        //Command = GET_FULLSTATE; 

        sent = printer.Send( _HTTP->client() , 200, "text/json", std::bind(&HueBridge::Send_DataStore_Callback, this) ); 
        return; 
    } else if ( _HTTP->uri().indexOf(user) != -1 && _HTTP->uri().endsWith("/state") ) {

        if (RequestMethod == HTTP_PUT) { 
          //Command = PUT_LIGHT;
          //Serial.print("PUT_LIGHT"); 
          Put_light();

        } else if (RequestMethod == HTTP_GET) {
          //Command = GET_LIGHT; 
          Serial.print("GET_LIGHT - toDo"); // ToDo
          _HTTP->send(404);

        } else {
          Serial.print("LIGHT Unknown req: ");
          Serial.print(HTTPMethod_text[RequestMethod]);
          _HTTP->send(404);

        }
        return; 
    } else if ( _HTTP->uri().indexOf(user) != -1 && _HTTP->uri().indexOf("/groups/") != -1 )  { // old _HTTP->uri().endsWith("/action")

        if (RequestMethod == HTTP_PUT) { 
          //Command = PUT_GROUP;
          //Serial.print("PUT_GROUP"); 
          Put_group();

        } else if (RequestMethod == HTTP_GET) {
          //Command = GET_GROUP; 
          Serial.print("GET_GROUP- todo"); // ToDo
          _HTTP->send(404);

        } else {
          Serial.print("GROUP Unknown req: ");
          Serial.print(HTTPMethod_text[RequestMethod]);
          _HTTP->send(404);
        }
        return; 
	} else if (_HTTP->uri().indexOf(user) != -1 && _HTTP->uri().endsWith("/groups") ) {
	
	    if (RequestMethod == HTTP_PUT) { 
          //Command = ADD_GROUP;
          Serial.println("ADD_GROUP");
          Add_Group();

     	 }


     	return; 

	} else if ( _HTTP->uri().substring(0, _HTTP->uri().lastIndexOf("/") ) == "/api/" + user + "/lights" ) {

      if (RequestMethod == HTTP_PUT) { 
          //Command = PUT_LIGHT_ROOT;
          Serial.println("PUT_LIGHT_ROOT"); 
          Put_Light_Root();

        } else {
          //Command = GET_LIGHT_ROOT;
          Serial.println("GET_LIGHT_ROOT - todo"); 
          _HTTP->send(404);
        }
        return; 
    } else if  ( _HTTP->uri() == "/api/" + user + "/lights" ) {
         //Command = GET_ALL_LIGHTS; 
         Serial.println("GET_ALL_LIGHTS- todo"); 
         _HTTP->send(404);
    	return; 
    }


    	 
    else 

    {

        Serial.print("UnknownUri: ");
        Serial.print(_HTTP->uri());
        Serial.print(" ");
        Serial.println(HTTPMethod_text[RequestMethod]); 

        if (_HTTP->arg("plain") != "" ) {
          Serial.print("BODY: ");
          Serial.println(_HTTP->arg("plain"));
        }
        _HTTP->send(404);
        return; 

    }

    //------------------------------------------------------
    //                    END OF NEW command PARSING
    //------------------------------------------------------


  // long _time = millis() - startime; 
  // Serial.print(" "); 
  // Serial.print(_time);
  // Serial.println("ms");

	//Serial.println();

  	}



/*-------------------------------------------------------------------------------------------

Functions to send out JSON data

-------------------------------------------------------------------------------------------*/


void HueBridge::Send_DataStore_Callback() {

            printer.print("{"); // START of JSON 
              printer.print(F("\"lights\":{ "));
                  Print_Lights();  // Lights
              printer.print("},");
              printer.print(F("\"config\":{ "));
                  Print_Config();  // Config
              printer.print("},");
              printer.print(F("\"groups\": { "));
                  Print_Groups();  // Config
              printer.print(F("},")); 
              printer.print(F("\"scenes\" : { }"));
              printer.print(","); 
              printer.print(F("\"schedules\" : { }"));       
          	printer.print("}"); //  END of JSON
}


void HueBridge::Send_Config_Callback () {

          printer.print("{"); // START of JSON 
            Print_Config();  // Config      
          printer.print("}"); //  END of JSON

}

void HueBridge::Print_Lights() {

      for (uint8_t i = 0; i < _LightCount; i++) {
          
          if (i > 0 ) printer.print(",");

          uint8_t LightNumber = i + 1; 
          HueLight* currentlight = &Lights[i];

          printer.print(F("\""));
          printer.print(LightNumber);
          printer.print(F("\":{"));
          printer.print(F("\"type\":\"Extended color light\","));
          printer.printf("\"name\":\"%s\",",currentlight->Name);
          printer.print(F("\"modelid\":\"LST001\","));
          printer.print(F("\"state\":{"));
          ( currentlight->State == true ) ? printer.print(F("\"on\":true,")) : printer.print(F("\"on\":false,"));
          printer.printf("\"bri\":%u,", currentlight->hsb.B);
          printer.printf("\"hue\":%u,", currentlight->hsb.H);
          printer.printf("\"sat\":%u,", currentlight->hsb.S);

          //temporarily holds data from vals
          char x[10];                
          char y[10];
          // clear the array
          memset(x,0,10);
          memset(y,0,10);

          // check is a number
          //  if isnan(currentlight->xy.x) currentlight->xy.x = 0;
          //  if isnan(currentlight->xy.y) currentlight->xy.y = 0;
          //4 is mininum width, 3 is precision; float value is copied onto buf
          if (!isnan(currentlight->xy.x)) dtostrf(currentlight->xy.x, 5, 4, x); else x[0] = '0'    ;    
          if (!isnan(currentlight->xy.y)) dtostrf(currentlight->xy.y, 5, 4, y); else y[0] = '0'    ;     
          
          //dtostrf(currentlight->xy.y, 5, 4, y);

          printer.printf("\"xy\":[%s,%s],", x, y );
          printer.printf("\"ct\":%u,", currentlight->Ct);
          printer.print(F("\"alert\":\"none\","));
          printer.print(F("\"effect\":\"none\","));
          printer.printf("\"colormode\":\"%s\",", currentlight->Colormode);
          printer.print(F("\"reachable\":true}"));
          printer.print("}");
      }

}

void HueBridge::Print_Groups(){

uint8_t groups_sent = 0;

      for (uint8_t GroupNumber = 1; GroupNumber < _GroupCount; GroupNumber++) { // start at 1 to NOT send group 0.. 
          
          HueGroup* currentgroup = &Groups[GroupNumber];

       if ( currentgroup->Inuse || GroupNumber == _nextfreegroup ) {   

          if (groups_sent > 0 ) printer.print(",");

          printer.print(F("\""));
          printer.print(GroupNumber);
          printer.print(F("\":{"));
          printer.printf("\"name\":\"%s\",",currentgroup->Name);
          printer.print(F("\"lights\": ["));

            for (uint8_t j = 0; j < currentgroup->LightsCount; j++) {
              if (j>0) printer.print(",");
              printer.printf("\"%u\"", currentgroup->LightMembers[j]);

            }

          printer.print(F("],"));
          printer.print(F("\"type\":\"LightGroup\","));
          printer.print(F("\"action\": {"));
            ( currentgroup->State == true ) ? printer.print(F("\"on\":true,")) : printer.print(F("\"on\":false,"));
          printer.printf("\"bri\":%u,", currentgroup->Bri);
          printer.printf("\"hue\":%u,", currentgroup->Hue);
          printer.printf("\"sat\":%u,", currentgroup->Sat);
          printer.print("\"effect\": \"none\",");

          //temporarily holds data from vals
          char x[10];                
          char y[10];
          //4 is mininum width, 3 is precision; float value is copied onto buff
          dtostrf(currentgroup->xy.x, 5, 4, x);
          dtostrf(currentgroup->xy.y, 5, 4, y);

          printer.printf("\"xy\":[%s,%s],", x, y );
          printer.printf("\"ct\":%u,", currentgroup->Ct);
          printer.print(F("\"alert\":\"none\","));
          printer.printf("\"colormode\":\"%s\"", currentgroup->Colormode);
          printer.print("}}");

          groups_sent++; // used for comma placement
      }

  }
      
}


void HueBridge::Print_Config() {

  printer.print("\"name\":\"Hue Emulator\",");
  printer.print("\"swversion\":\"0.1\","); 
  printer.printf("\"mac\": \"%s\",", (char*)_macString.c_str() );
  printer.print("\"dhcp\":true,"); 
  printer.printf("\"ipaddress\": \"%s\",", (char*)_ipString.c_str() );
  printer.printf("\"netmask\": \"%s\",", (char*)_netmaskString.c_str() );
  printer.printf("\"gateway\": \"%s\",", (char*)_gatewayString.c_str() );
  printer.print(F("\"swupdate\":{\"text\":\"\",\"notify\":false,\"updatestate\":0,\"url\":\"\"},")); 
  printer.print(F("\"whitelist\":{\"xyz\":{\"name\":\"clientname#devicename\"}}"));
          
}


void HueBridge::SendJson(JsonObject& root){

  size_t size = 0; // root.measureLength(); 
  //Serial.print("  JSON ");
  WiFiClient c = _HTTP->client();
  printer.Begin(c); 
  printer.SetHeader(200, "text/json");
  printer.SetCountMode(true);
  root.printTo(printer);
  size  = printer.SetCountMode(false);
  root.printTo(printer);
  c.stop();
  while(c.connected()) yield();
  printer.End(); // free memory...
  //Serial.printf(" %uB\n", size ); 

}

void HueBridge::SendJson(JsonArray& root){


  size_t size = 0; 
  //Serial.print("  JSON ");
  WiFiClient c = _HTTP->client();
  printer.Begin(c); 
  printer.SetHeader(200, "text/json");
  printer.SetCountMode(true);
  root.printTo(printer);
  size = printer.SetCountMode(false);
  root.printTo(printer);
  c.stop();
  while(c.connected()) yield();
  printer.End(); // free memory...
  //Serial.printf(" %uB\n", size); 

}


void HueBridge::Handle_Description() {

  String str = F("<root><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://"); 
  str += _ipString; 
  str += F(":80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType><friendlyName>Philips hue ("); 
  str += _ipString; 
  str += F(")</friendlyName><manufacturer>Royal Philips Electronics</manufacturer><manufacturerURL>http://www.philips.com</manufacturerURL><modelDescription>Philips hue Personal Wireless Lighting</modelDescription><modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL><serialNumber>00178817122c</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-00178817122c</UDN><presentationURL>index.html</presentationURL><iconList><icon><mimetype>image/png</mimetype><height>48</height><width>48</width><depth>24</depth><url>hue_logo_0.png</url></icon><icon><mimetype>image/png</mimetype><height>120</height><width>120</width><depth>24</depth><url>hue_logo_3.png</url></icon></iconList></device></root>");
  _HTTP->send(200, "text/plain", str);
  Serial.println(F("/Description.xml SENT")); 

}


/*-------------------------------------------------------------------------------------------

Functions to process known web request   PUT LIGHT

-------------------------------------------------------------------------------------------*/

void HueBridge::Put_light () {

  if (_HTTP->arg("plain") == "")
    {
      return; 
    }

    //------------------------------------------------------
    //                    Extract Light ID ==>  Map to LightNumber (toDo)
    //------------------------------------------------------

    uint8_t numberOfTheLight = Extract_LightID();  
    String lightID = String(numberOfTheLight + 1); 
 
    HueLight* currentlight = &Lights[numberOfTheLight];

    struct RgbColor rgb;

    //------------------------------------------------------
    //                    JSON set up IN + OUT 
    //------------------------------------------------------

    DynamicJsonBuffer jsonBufferOUT; // create buffer 
    DynamicJsonBuffer jsonBufferIN; // create buffer 

    JsonObject& root = jsonBufferIN.parseObject(_HTTP->arg("plain"));
    JsonArray& array = jsonBufferOUT.createArray(); // new method
  	
    //------------------------------------------------------
    //                    ON / OFF 
    //------------------------------------------------------

     bool onValue{false}, hasHue{false}, hasBri{false}, hasSat{false}, hasXy{false};  

   //  struct HueHSB &hsb = currentlight->hsb; // might be useful method for later.



      


    if (root.containsKey("on"))
        {
          onValue = root["on"]; 

          String response = F("/lights/"); response += lightID ; response +=  F("/state/on"); // + onoff; 

          if (onValue) {
	            currentlight->State = true;

            if (strcmp (currentlight->Colormode,"hs") == 0 ) {

            	rgb = HUEhsb2rgb(currentlight->hsb); //  designed to handle philips hue RGB ALLOCATED FROM JSON REQUEST
        	
        	} else if (strcmp (currentlight->Colormode,"xy") == 0) {
       			rgb = XYtorgb(currentlight->xy, currentlight->hsb.B);  // set the color to return
        	}

           if (_returnJSON) AddSucessToArray (array, response, F("on") ); 

          } else {

            currentlight->State = false;
            rgb = RgbColor(0,0,0);
            if (_returnJSON) AddSucessToArray (array, response, F("off") ) ; 

          }

        }   

    //------------------------------------------------------
    //              HUE / SAT / BRI 
    //------------------------------------------------------
        
    if (root.containsKey("hue"))
        {
          currentlight->hsb.H = root["hue"]; 
          String response = "/lights/" + lightID + "/state/hue"; // + onoff; 
          hasHue = true; 
          if (_returnJSON) AddSucessToArray (array, response, root["hue"] ); 
        } 

    if (root.containsKey("sat"))
        {
          currentlight->hsb.S = root["sat"]; 
          String response = "/lights/" + lightID + "/state/sat"; // + onoff; 
          hasSat = true; 
          if (_returnJSON) AddSucessToArray (array, response, root["sat"]); 

        } 

    if (root.containsKey("bri"))
        {
          currentlight->hsb.B = root["bri"]; 
          String response = "/lights/" + lightID + "/state/bri"; // + onoff; 
          hasBri = true; 
          if (_returnJSON) AddSucessToArray (array, response, root["bri"]); 
        } 

    //------------------------------------------------------
    //              XY Color Space 
    //------------------------------------------------------



    if (root.containsKey("xy"))
        {
           currentlight->xy.x = root["xy"][0];
           currentlight->xy.y = root["xy"][1];
           hasXy = true; 

        if (_returnJSON) {
          JsonObject& nestedObject = array.createNestedObject();          
          JsonObject& sucess = nestedObject.createNestedObject("success");
          JsonArray&  xyObject  = sucess.createNestedArray("xy");
          xyObject.add(currentlight->xy.x, 4);  // 4 digits: "3.1415"
          xyObject.add(currentlight->xy.x, 4);  // 4 digits: "3.1415"
      	  }
        }

    //------------------------------------------------------
    //              Ct Colour space
    //------------------------------------------------------

    if (root.containsKey("ct"))
        {

        	// todo


		}

    //------------------------------------------------------
    //              Apply recieved Color data 
    //------------------------------------------------------

    if (hasHue || hasBri || hasSat) {
      rgb = HUEhsb2rgb(currentlight->hsb); //  designed to handle philips hue RGB ALLOCATED FROM JSON REQUEST
      if (!hasXy) currentlight->xy = HUEhsb2xy(currentlight->hsb); // COPYED TO LED STATE incase onother applciation requests colour
      strcpy(currentlight->Colormode, "hs");

   	} 

    if (hasXy) {
      	rgb = XYtorgb(currentlight->xy, currentlight->hsb.B);  // set the color to return
      	currentlight->hsb = xy2HUEhsb(currentlight->xy, currentlight->hsb.B);  // converts for storage...
      	strcpy(currentlight->Colormode, "xy");
   	}    


    //------------------------------------------------------
    //              SEND Back changed JSON REPLY 
    //------------------------------------------------------

    // Serial.println();
    // array.prettyPrintTo(Serial);
    // Serial.println(); 

    if (_returnJSON) SendJson(array);

    //printer.print("]"); 
    //printer.Send_Buffer(200,"text/json");
    //-------------------------------------
    //              Print Saved State Buffer to serial 
    //------------------------------------------------------

      // Serial.print("Saved = Hue:");
      // Serial.print(StripHueData[numberOfTheLight].Hue); 
      // Serial.print(", Sat:"); 
      // Serial.print(StripHueData[numberOfTheLight].Sat); 
      // Serial.print(", Bri:");   
      // Serial.print(StripHueData[numberOfTheLight].Bri); 
      // Serial.println(); 


    //------------------------------------------------------
    //              Set up LEDs... rock and roll.... 
    //------------------------------------------------------

    	_HandlerNEW(numberOfTheLight + 1, rgb, currentlight); 

    //------------------------------------------------------
    //              Save Settings to SPIFFS
    //------------------------------------------------------

  		long start_time_spiffs = millis(); 

		File configFile = SPIFFS.open("/lights.conf", "r+");
  		
  		if (!configFile)
  			{
    			Serial.println(F("Failed to open test.conf, making new")); 
    			configFile = SPIFFS.open("/lights.conf", "w+");
  			
  			} else {
  			
  				Serial.println(F("Opened Hue_conf.txt for UPDATE...."));

  				unsigned char * data = reinterpret_cast<unsigned char*>(Lights); // use unsigned char, as uint8_t is not guarunteed to be same width as char...
  				size_t bytes = configFile.write(data, sizeof(HueLight) * _LightCount ); // C++ way

  				configFile.close();
  				Serial.print("TIME TAKEN: ");
  				Serial.print(millis() - start_time_spiffs);
  				Serial.print("ms, ");
  				Serial.print(bytes);
  				Serial.println("B"); 
  			}

    /*---------------------------------------------------------------------------------------------------------------------------------------
                    Experimental - Per Light saving of settings..  move towards file system based storage, for all hue data. 
  					This is working.. saves only the current light to SPIFFS... 		
    //---------------------------------------------------------------------------------------------------------------------------------------*/ 			


#ifdef EXPERIMENTAL 

		File testFile;

		long t1 = micros();
		long h1 = ESP.getFreeHeap();

		testFile = SPIFFS.open("/test.conf", "r+");

		File lightFile = SPIFFS.open("/lights.conf", "r+");
		File groupFile = SPIFFS.open("/groups.conf", "r+");


		Serial.printf("Open file time: %uus, heap use = %u \n", micros() - t1 , h1 - ESP.getFreeHeap());

  		long start_spiffs = micros(); 

		if (!testFile) {
			testFile = SPIFFS.open("/test.conf", "w+");
		} 

		if (testFile) {

			uint32_t position = numberOfTheLight * sizeof(HueLight);

			if(!testFile.seek(position, SeekSet)) { // if seek fails... ie file is too short
				testFile.seek(0, SeekEnd); // go to end of file
					do {
						testFile.write(0); // write 0 until in right place... 
					} while (testFile.position() < position); 
			}

			unsigned char * data = reinterpret_cast<unsigned char*>(currentlight); // C++ way
  			size_t bytes = testFile.write(data, sizeof(HueLight)); 

  			Serial.printf("newFS Time taken %uus, %uB \n", micros() - start_spiffs, bytes);
  			

		long t2 = micros();
		long h2 = ESP.getFreeHeap();
  		
  		testFile.close();

		Serial.printf("Open close time: %uus, heap use = %u \n", micros() - t2, ESP.getFreeHeap() - h2);

  		} else {	
  			Serial.println("FAILED TO WRITE TO + CREATE FILE");
  		}
#endif

}

/*-------------------------------------------------------------------------------------------

Functions to process known web request   PUT GROUP 

-------------------------------------------------------------------------------------------*/

void HueBridge::Put_group () {

  if (_HTTP->arg("plain") == "")
    {
      return; 
    }

     // Serial.print("\nREQUEST: ");
     // Serial.println(_HTTP->uri());
     // Serial.println(_HTTP->arg("plain"));


    //------------------------------------------------------
    //                    Extract Light ID ==>  Map to LightNumber (toDo)
    //------------------------------------------------------

    //uint8_t numberOfTheGroup = Extract_LightID();  
    
    uint8_t groupNum = atoi(subStr(_HTTP->uri().c_str(), (char*) "/", 4));   //   

    uint8_t numberOfTheGroup = groupNum; 
    String groupID = String(groupNum); 

    // Serial.print("\nGr = ");
    // Serial.print(groupID);
    // Serial.print(" _RunningGroupCount = ");
    // Serial.println(_RunningGroupCount);

    if (numberOfTheGroup > _GroupCount) return; 

    HueGroup* currentgroup;
    currentgroup = &Groups[groupNum];

    struct RgbColor rgb;
    struct HueHSB hsb; 

	bool colourschanged = false;   
    //------------------------------------------------------
    //                    JSON set up IN + OUT 
    //------------------------------------------------------

    DynamicJsonBuffer jsonBufferOUT; // create buffer 
    DynamicJsonBuffer jsonBufferIN; // create buffer 
    JsonObject& root = jsonBufferIN.parseObject(_HTTP->arg("plain"));
    JsonArray& array = jsonBufferOUT.createArray(); // new method

    //------------------------------------------------------
    //                    ON / OFF 
    //------------------------------------------------------

    bool onValue = false; 

    if (root.containsKey("on"))
        {
          onValue = root["on"]; 
          String response = "/groups/" + groupID + "/state/on"; // + onoff; 

          if (onValue) {
//            Serial.print("  ON :");
            currentgroup->State = true;
            if (_returnJSON) AddSucessToArray (array, response, F("on") ); 

          } else {
//            Serial.print("  OFF :");
            currentgroup->State = false;
            rgb = RgbColor(0,0,0);
            if (_returnJSON) AddSucessToArray (array, response, F("off") ) ; 

          }
          colourschanged = true; 

        }   

    //------------------------------------------------------
    //              HUE / SAT / BRI 
    //------------------------------------------------------
        //    To Do Colormode..... 

     bool hasHue{false}, hasBri{false}, hasSat{false};
     
     uint16_t hue = currentgroup->Hue ;
     uint8_t sat = currentgroup->Sat ; 
     uint8_t bri = currentgroup->Bri ; 
        
    if (root.containsKey("hue"))
        {
          hue = root["hue"]; 
          String response = "/groups/" + groupID + "/state/hue"; // + onoff; 
//          Serial.print("  HUE -> ");
//          Serial.print(hue);
//          Serial.print(", "); 
          hasHue = true; 
          if (_returnJSON) AddSucessToArray (array, response, root["hue"] ); 
        } 

    if (root.containsKey("sat"))
        {
          sat = root["sat"]; 
          String response = "/groups/" + groupID + "/state/sat"; // + onoff; 
//          Serial.print("  SAT -> ");
//          Serial.print(sat);
//          Serial.print(", "); 
          hasSat = true; 
          if (_returnJSON) AddSucessToArray (array, response, root["sat"]); 
        } 

    if (root.containsKey("bri"))
        {
          bri = root["bri"]; 
          String response = "/groups/" + groupID + "/state/bri"; // + onoff; 
//          Serial.print("  BRI -> ");
//          Serial.print(bri);
//          Serial.print(", "); 
          hasBri = true; 
          if (_returnJSON) AddSucessToArray (array, response, root["bri"]); 
        } 

    //------------------------------------------------------
    //              XY Color Space 
    //------------------------------------------------------

     HueXYColor xy_instance; 
     bool hasXy{false}; 

    if (root.containsKey("xy"))
        {
           xy_instance.x = root["xy"][0];
           xy_instance.y = root["xy"][1];
           currentgroup->xy = xy_instance;
              // Serial.print("  XY (");
              // Serial.print(xy_instance.x);
              // Serial.print(",");
              // Serial.print(xy_instance.y);
              // Serial.print(") "); 
            hasXy = true; 
        if (_returnJSON) {
          JsonObject& nestedObject = array.createNestedObject();          
          JsonObject& sucess = nestedObject.createNestedObject("success");
          JsonArray&  xyObject  = sucess.createNestedArray("xy");
          xyObject.add(xy_instance.x, 4);  // 4 digits: "3.1415"
          xyObject.add(xy_instance.y, 4);  // 4 digits: "3.1415"
      	  }
        }

    //------------------------------------------------------
    //              Ct Colour space
    //------------------------------------------------------

    if (root.containsKey("ct"))
        {




		}

    //------------------------------------------------------
    //              Apply recieved Color data 
    //------------------------------------------------------

    if (hasHue) currentgroup->Hue = hsb.H = hue;
    if (hasBri) currentgroup->Sat = hsb.S = sat;
    if (hasSat) currentgroup->Bri = hsb.B = bri;

    if (hasHue || hasSat || hasBri) {
      rgb = HUEhsb2rgb(hsb); //  designed to handle philips hue RGB ALLOCATED FROM JSON REQUEST
      currentgroup->xy = HUEhsb2xy(hsb); // COPYED TO LED STATE incase onother applciation requests colour
      colourschanged = true; 
   } else if (hasXy) {

      rgb = XYtorgb(xy_instance, bri);  // set the color to return
      hsb = xy2HUEhsb(xy_instance, bri);  // converts for storage...

      currentgroup->Hue = hsb.H; ///floor(hsb.H * 182.04 * 360.0); 
      currentgroup->Sat = hsb.S; //floor(hsb.S * 254);
      currentgroup->Bri = hsb.B; // floor(hsb.B * 254);  
   	  colourschanged = true; 
   }    


    //------------------------------------------------------
    //              Group NAME
    //------------------------------------------------------

    if (root.containsKey("name")) {

		Name_Group(groupNum, root["name"]);
        String response = "/groups/" + groupID + "/name/"; // + onoff; 
        if (_returnJSON) AddSucessToArray (array, response, root["name"]); 

    }

    //------------------------------------------------------
    //              Group LIGHTS
    //------------------------------------------------------

    if (root.containsKey("lights")) {
    	
       	JsonArray& array2 = root["lights"];
    	uint8_t i = 0;

			for(JsonArray::iterator it=array2.begin(); it!=array2.end(); ++it) 
					{
    					uint8_t value = atoi(it->as<const char*>());    
    						if (i < MaxLightMembersPerGroup) {
    						currentgroup->LightMembers[i] = value; 
    						}
    					i++;
					}

			currentgroup->LightsCount = i;
			if (i == 0 && groupNum > 1) currentgroup->Inuse = false; else currentgroup->Inuse = true; // added group number check so groups 0+1 are always in use..
			_nextfreegroup = find_nextfreegroup(); 
    }

    //------------------------------------------------------
    //              SEND Back changed JSON REPLY 
    //------------------------------------------------------

      // char *msgStr = aJson.print(reply);
      // aJson.deleteItem(reply);
      // Serial.print("\nJSON REPLY = ");
      // //Serial.println(millis());
      // Serial.println(msgStr);
      // HTTP.send(200, "text/plain", msgStr);
      // free(msgStr);


    // Serial.println("RESPONSE:");
    // array.prettyPrintTo(Serial);
    // Serial.println(); 

    if (_returnJSON) SendJson(array);


    //------------------------------------------------------
    //              Print Saved State Buffer to serial 
    //------------------------------------------------------

      // Serial.print("Saved = Hue:");
      // Serial.print(StripHueData[numberOfTheLight].Hue); 
      // Serial.print(", Sat:"); 
      // Serial.print(StripHueData[numberOfTheLight].Sat); 
      // Serial.print(", Bri:");   
      // Serial.print(StripHueData[numberOfTheLight].Bri); 
      // Serial.println(); 


    //------------------------------------------------------
    //              Set up LEDs... rock and roll.... 
    //------------------------------------------------------


    	uint8_t Group = (uint8_t)groupID.toInt();

    //	_Handler(Light, 2000, rgb); 

    if (colourschanged) {

    	if (Group == 0 ) {

    		for (uint8_t i = 0; i < _LightCount; i++) {

	    		HueLight* currentlight;
   	 			currentlight = &Lights[i]; // values stored in array are Hue light numbers so + 1; 
   		 		_HandlerNEW(i+1, rgb, currentlight); 
 //   			currentlight->Hue = currentgroup->Hue;
 //   			currentlight->Sat = currentgroup->Sat;
//    			currentlight->Bri = currentgroup->Bri;
    			currentlight->xy = currentgroup->xy; 
    			currentlight->State = currentgroup->State; 

	    	}

    	} else {

    		for (uint8_t i = 0; i < currentgroup->LightsCount; i++) {

    			HueLight* currentlight;
    			currentlight = &Lights[currentgroup->LightMembers[i] - 1]; // values stored in array are Hue light numbers so + 1; 
    			_HandlerNEW(currentgroup->LightMembers[i], rgb, currentlight); 
//    			currentlight->Hue = currentgroup->Hue;
//    			currentlight->Sat = currentgroup->Sat;
//    			currentlight->Bri = currentgroup->Bri;
    			currentlight->xy = currentgroup->xy; 
    			currentlight->State = currentgroup->State; 

    		}

		}

	}


    //------------------------------------------------------
    //              Save Settings to SPIFFS
    //------------------------------------------------------

  		long start_time_spiffs = millis(); 

		File configFile = SPIFFS.open("/groups.conf", "w+");
  		
  		if (!configFile)
  			{
    			Serial.println(F("Failed to open groups.conf")); 
  			} else {
  				Serial.println(F("Opened groups.conf for UPDATE...."));
  				Serial.printf("Start Position =%u \n", configFile.position());

  				unsigned char * data = reinterpret_cast<unsigned char*>(Groups); // use unsigned char, as uint8_t is not guarunteed to be same width as char...
  				
  				size_t bytes = configFile.write(data, sizeof(HueGroup) * _GroupCount ); // C++ way

  				Serial.printf("END Position =%u \n", configFile.position());

  				configFile.close();
  				Serial.print("TIME TAKEN: ");
  				Serial.print(millis() - start_time_spiffs);
  				Serial.print("ms, ");
  				Serial.print(bytes);
  				Serial.println("B"); 
  			}







}


void HueBridge::Put_Light_Root() { 

    //------------------------------------------------------
    //              Set Light Name 
    //------------------------------------------------------
    DynamicJsonBuffer jsonBufferOUT; // create buffer 
    DynamicJsonBuffer jsonBufferIN; // create buffer 


    JsonObject& root = jsonBufferIN.parseObject( _HTTP->arg("plain"));
    JsonArray& array = jsonBufferOUT.createArray(); // new method

    uint8_t numberOfTheLight = Extract_LightID();
    String lightID = String(numberOfTheLight + 1); 

    if (root.containsKey("name"))
        {
           Name_Light(numberOfTheLight, root["name"]) ;

           String response = "/lights/" + lightID + "/name"; // + onoff; 
           AddSucessToArray (array, response, root["name"]); 
        }

        //array.prettyPrintTo(Serial);

        SendJson(array);

}

void HueBridge::Get_Light_Root() { 

    //------------------------------------------------------
    //              Get Light State 
    //------------------------------------------------------
    DynamicJsonBuffer jsonBufferOUT; // create buffer 
    JsonObject& object = jsonBufferOUT.createObject(); 

    uint8_t LightID = Extract_LightID();
    //String numberOfTheLight = String(LightID + 1); 

  //  Add_light(object, LightID); 

    SendJson(object);


}

bool HueBridge::Add_Group() {

	//im not entirely sure that chroma uses this...  

	Serial.println("ADD GROUP HIT");
	
	  if (_HTTP->arg("plain") == "") return 0; 
	  if (_nextfreegroup == 0 ) return 0 ; // need to add sending error....


	HueGroup* currentgroup; 

	  for (uint8_t i = 0; i <= _GroupCount; i++){
			currentgroup = &Groups[i];
			if (!currentgroup->Inuse) break; 
			if (i == _GroupCount) return 0; // if they are all full return...
	  }


    DynamicJsonBuffer jsonBufferOUT; // create buffer 
    DynamicJsonBuffer jsonBufferIN; // create buffer 


    JsonObject& root = jsonBufferIN.parseObject( _HTTP->arg("plain"));
    JsonArray& array = jsonBufferOUT.createArray(); // new method	  
    
    //------------------------------------------------------
    //              Group NAME
    //------------------------------------------------------

    if (root.containsKey("name")) {

//		Name_Group(_RunningGroupCount, root["name"]);
		Name_Group(currentgroup, root["name"]);

 //       String response = "/groups/" + groupID + "/name/"; // + onoff; 
 //       if (_returnJSON) AddSucessToArray (array, response, root["name"]); 

    }

    //------------------------------------------------------
    //              Group LIGHTS
    //------------------------------------------------------

    if (root.containsKey("lights")) {
    	
    	//Serial.println();

    	JsonArray& array2 = root["lights"];

    	uint8_t i = 0;

			for(JsonArray::iterator it=array2.begin(); it!=array2.end(); ++it) 
					{
    			uint8_t value = atoi(it->as<const char*>());    

    			if (i < MaxLightMembersPerGroup) {

    				currentgroup->LightMembers[i] = value; 
    			
    			//	Serial.print(i);
    			//	Serial.print(" = ");
    			//	Serial.println(value);

    			}

    			i++;

				}

				currentgroup->LightsCount = i;

    }


//	  _RunningGroupCount++;

	uint8_t freegroups = 0; 

	  for (uint8_t i = 0; i < _GroupCount; i++){
			HueGroup* currentgroup = &Groups[i];
			if (!currentgroup->Inuse) freegroups++; 
	  }

	  Serial.printf("Free groups remaining %u\n", freegroups); 

      _nextfreegroup = find_nextfreegroup(); 


}

void HueBridge::Name_Light(uint8_t i, const char* name) {

  if (i > _LightCount) return; 
  HueLight* currentlight;
  currentlight = &Lights[i];
  memset(currentlight->Name, 0, sizeof(currentlight->Name));
  //memcpy(StripHueData[i].name, name, sizeof(name) ); 
  strcpy(currentlight->Name, name ); 
}

void HueBridge::Name_Light(uint8_t i,  String &name) {
    
    if (i > _LightCount) return; 
    HueLight* currentlight;
    currentlight = &Lights[i];
    name.toCharArray(currentlight->Name, 32);

}



void HueBridge::Name_Group(HueGroup * currentgroup, const char* name) {

  //if (i > _GroupCount) return; 
  //HueGroup* currentgroup;
  //currentgroup = &Groups[i];
  memset(currentgroup->Name, 0, sizeof(currentgroup->Name));
  //memcpy(StripHueData[i].name, name, sizeof(name) ); 
  strcpy(currentgroup->Name, name ); 
}



void HueBridge::Name_Group(uint8_t i, const char* name) {

  if (i > _GroupCount) return; 
  HueGroup* currentgroup;
  currentgroup = &Groups[i];
  memset(currentgroup->Name, 0, sizeof(currentgroup->Name));
  //memcpy(StripHueData[i].name, name, sizeof(name) ); 
  strcpy(currentgroup->Name, name ); 
}


// void HueBridge::Name_Group(uint8_t i,  String &name) {

//     if (i > _GroupCount) return; 
//     HueGroup* currentgroup;
//     currentgroup = &Groups[i];
//     name.toCharArray(currentgroup->Name, 32);

// }

uint8_t HueBridge::Extract_LightID() {

	String ID; 
	if (_HTTP->uri().indexOf("/lights/") > -1) {
	    ID = (_HTTP->uri().substring(_HTTP->uri().indexOf("/lights/") + 8 ,_HTTP->uri().indexOf("/state")));
	} else if (_HTTP->uri().indexOf("/groups/") > -1) {
		ID = (_HTTP->uri().substring(_HTTP->uri().indexOf("/groups/") + 8 ,_HTTP->uri().indexOf("/action")));
	} else return 0; 

    uint8_t IDint  = ID.toInt() - 1; 
    //Serial.print(" LIGHT ID = ");
    //Serial.println(lightID);
    return IDint ; 
}

// New method using arduinoJSON
void HueBridge::AddSucessToArray(JsonArray& array, String item, String value) {

      JsonObject& nestedObject = array.createNestedObject(); 
      JsonObject& sucess = nestedObject.createNestedObject("success");
      sucess[item] = value; 

}

void HueBridge::AddSucessToArray(JsonArray& array, String item,  char* value) {

      JsonObject& nestedObject = array.createNestedObject(); 
      JsonObject& sucess = nestedObject.createNestedObject("success");
      sucess[item] = value; 

}

String HueBridge::StringIPaddress(IPAddress myaddr) {


  String LocalIP = "";
  for (int i = 0; i < 4; i++)
  {
    LocalIP += String(myaddr[i]);
    if (i < 3) LocalIP += ".";
  }
  return LocalIP;

}

void HueBridge::initSSDP() {

  Serial.printf("Starting SSDP...");
  SSDP.begin();
  SSDP.setSchemaURL((char*)"description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName((char*)"Philips hue clone");
  SSDP.setSerialNumber((char*)"001788102201");
  SSDP.setURL((char*)"index.html");
  SSDP.setModelName((char*)"Philips hue bridge 2012");
  SSDP.setModelNumber((char*)"929000226503");
  SSDP.setModelURL((char*)"http://www.meethue.com");
  SSDP.setManufacturer((char*)"Royal Philips Electronics");
  SSDP.setManufacturerURL((char*)"http://www.philips.com");
  Serial.println("SSDP Started");
}

void HueBridge::SetReply(bool value) {
	_returnJSON = value; 
}

bool HueBridge::SetLightState(uint8_t light, bool value){

	if(light == 0) return false;
	light--;
	if (light < 0 || light > _LightCount) return false; 
	HueLight* currentlight = &Lights[light]; 
    currentlight->State = value; 
    return true; 
}

bool HueBridge::GetLightState(uint8_t light) {

	if(light == 0) return false;
	light--;
	if (light < 0 || light > _LightCount) return false; 
	HueLight* currentlight = &Lights[light]; 
	return currentlight->State;

}

bool HueBridge::SetLightRGB(uint8_t light, RgbColor color) {

//
//					To Do
//

}

RgbColor HueBridge::GetLightRGB(uint8_t light) {


//
//					To Do
//

}




bool HueBridge::SetGroupState(uint8_t group, bool value){

	if(group == 0) return false;
	group--;
	if (group < 0 || group > _GroupCount) return false; 
	HueGroup* currentgroup = &Groups[group];
    currentgroup->State = value; 
    return true; 
}



bool HueBridge::GetGroupState(uint8_t group) {

	if(group == 0) return false;
	group--;
	if (group < 0 || group > _GroupCount) return false; 
	HueGroup* currentgroup = &Groups[group];
    return currentgroup->State; 
}




/*-------------------------------------------------------------------------------------------

Colour Management
Thanks to probonopd

-------------------------------------------------------------------------------------------*/


struct HueHSB HueBridge::rgb2HUEhsb(RgbColor color)
{

  HsbColor hsb = HsbColor(color);
  int hue, sat, bri;

  hue = floor(hsb.H * 182.04 * 360.0);
  sat = floor(hsb.S * 254);
  bri = floor(hsb.B * 254);

  HueHSB hsb2;
  hsb2.H = hue;
  hsb2.S = sat;
  hsb2.B = bri;

  return (hsb2);
}



struct RgbColor HueBridge::HUEhsb2rgb(HueHSB color)
{

  float H, S, B;
  H = color.H / 182.04 / 360.0;
  S = color.S / 254.0; 
  B = color.B / 254.0; 
  return HsbColor(H, S, B);
}

struct HueXYColor HueBridge::rgb2xy(RgbColor color) {

        float red   =  float(color.R) / 255.0f;
        float green =  float(color.G) / 255.0f;
        float blue  =  float(color.B) / 255.0f;

        // Wide gamut conversion D65
        float r = ((red > 0.04045f) ? (float) pow((red + 0.055f)
                / (1.0f + 0.055f), 2.4f) : (red / 12.92f));
        float g = (green > 0.04045f) ? (float) pow((green + 0.055f)
                / (1.0f + 0.055f), 2.4f) : (green / 12.92f);
        float b = (blue > 0.04045f) ? (float) pow((blue + 0.055f)
                / (1.0f + 0.055f), 2.4f) : (blue / 12.92f);

        float x = r * 0.649926f + g * 0.103455f + b * 0.197109f;
        float y = r * 0.234327f + g * 0.743075f + b * 0.022598f;
        float z = r * 0.0000000f + g * 0.053077f + b * 1.035763f;

        // Calculate the xy values from the XYZ values
        
        HueXYColor xy_instance;

        xy_instance.x = x / (x + y + z);
        xy_instance.y = y / (x + y + z);

        if (isnan(xy_instance.x) ) {
            xy_instance.x = 0.0f;
        }
        if (isnan(xy_instance.y)) {
            xy_instance.y = 0.0f;
        }
 
  return xy_instance; 


}

struct HueXYColor HueBridge::HUEhsb2xy(HueHSB color) {

	RgbColor rgb = HUEhsb2rgb(color);
	double r = rgb.R / 255.0;
	double g = rgb.G / 255.0;
	double b = rgb.B / 255.0;
	double X = r * 0.649926f + g * 0.103455f + b *0.197109f;
	double Y = r * 0.234327f + g * 0.743075f + b * 0.022598f;
	double Z = r * 0.0000000f + g * 0.053077f + b * 1.035763f;
	HueXYColor xy;
	xy.x = X / (X + Y + Z);
	xy.y = Y / (X + Y + Z);
	return xy;
}

struct HueHSB HueBridge::xy2HUEhsb(HueXYColor xy, uint8_t bri) {

	double x = xy.x;
	double y = xy.y; 

	double z = 1.0f - xy.x - xy.y;
	double Y = (double)(bri / 254.0); // The given brightness value
	double X = (Y / xy.y) * xy.x;
	double Z = (Y / xy.y) * z;
	double r = X * 1.4628067f - Y * 0.1840623f - Z * 0.2743606f;
	double g = -X * 0.5217933f + Y* 1.4472381f + Z *  0.0677227f;
	double b = X * 0.0349342f - Y * 0.0968930f + Z * 1.2884099f;
	uint8_t R = abs(r) * 255;
	uint8_t G = abs(g) * 255;
	uint8_t B = abs(b) * 255;
	struct HueHSB hsb;

	double mi, ma, delta, h;
	mi = (R<G)?R:G; mi = (mi<B)?mi:B; ma = (R>G)?R:G;
	ma = (ma>B)?ma:B;
	delta = ma - mi;
	
	if(ma <= 0.0){
		hsb.H = 0xFFFF;
		hsb.S = 1;
		hsb.B = bri;
	return hsb;
	}

 	if (R >= ma) h = (G - B) / delta; // between yellow & magenta
	else if(G >= ma) h = 2.0 + (B - R) / delta; // between cyan & yellow
	else h = 4.0 + ( R - G ) / delta; // between magenta & cyan
	h *= 60.0; // degrees
	if(h < 0.0) h += 360.0;
	hsb.H = (uint16_t)floor(h * 182.04);
	hsb.S = (uint16_t)floor((delta / ma) * 254);
	hsb.B = bri;
	return hsb;
}

// struct HueHSB ct2hsb(long kelvin, uint8_t bri) {
// 	double r, g, b;
// 	long temperature = kelvin / 10;
// 	if(temperature <= 66) {
// 		r = 255;
// 	}
// 	else {
// 		r = temperature - 60;
// 		r = 329.698727446 * pow(r, -0.1332047592);
// 		if(r < 0) r = 0;
// 		if(r > 255) r = 255;
// 	}

// 	if(temperature <= 66) {
// 		g = temperature;
// 		g = 99.4708025861 log(g) - 161.1195681661;
// 		if(g < 0) g = 0;
// 		if(g > 255) g = 255;
// 		}
// 		else {
// 			g = temperature - 60;
// 			g = 288.1221695283 pow(g, -0.0755148492);
// 			if(g < 0) g = 0;
// 			if(g > 255) g = 255;
// 		}

// 		if(temperature >= 66) {
// 			b = 255;
// 		}
// 		else {
// 			if(temperature <= 19) {
// 			b = 0;
// 			}
// 		else {
// 			b = temperature - 10;
// 			b = 138.5177312231 * log(b) - 305.0447927307;
// 			if(b < 0) b = 0;
// 			if(b > 255) b = 255;
// 		}
// 		}

// 	uint8_t R = abs(r) 255;
// 	uint8_t G = abs(g) 255;
// 	uint8_t B = abs(b) * 255;
// 	struct HueHSB hsb;
// 	double mi, ma, delta, h;
// 	mi = (R<G)?R:G; mi = (mi<B)?mi:B; ma = (R>G)?R:G;
// 	ma = (ma>B)?ma:B;
// 	delta = ma - mi;
// 	if(ma <= 0.0){
// 	hsb.H = 0xFFFF;
// 	hsb.S = 1;
// 	hsb.B = bri;
// return hsb;
// }

// if(R >= ma) h = (G - B) / delta; // between
// }

//http://www.tannerhelland.com/4435/convert-temperature-rgb-algorithm-code/
// 'Given a temperature (in Kelvin), estimate an RGB equivalent

struct RgbColor HueBridge::ct2rbg(long tmpKelvin, uint8_t bri) {

     if (tmpKelvin < 1000) tmpKelvin = 1000;
     if (tmpKelvin > 40000)tmpKelvin = 40000;

     double tmpCalc; 
     RgbColor rgb; 
     tmpKelvin = tmpKelvin / 100; 

// RED.
     if (tmpKelvin <= 66) {
     	rgb.R = 255; 
     } else {
        tmpCalc = tmpKelvin - 60;
        tmpCalc = 329.698727446 * pow(tmpCalc, -0.1332047592);
        rgb.R = tmpCalc;
        if (rgb.R < 0) rgb.R = 0;
        if (rgb.R > 255) rgb.R = 255;

     }
// green
	if (tmpKelvin <= 66) {
//         'Note: the R-squared value for this approximation is .996
         tmpCalc = tmpKelvin;
         tmpCalc = 99.4708025861 * log(tmpCalc) - 161.1195681661;
         rgb.G = tmpCalc;
         if (rgb.G < 0) rgb.G = 0;
         if (rgb.G > 255) rgb.G = 255;
   } else {
//         'Note: the R-squared value for this approximation is .987
	         tmpCalc = tmpKelvin - 60; 
	         tmpCalc = 288.1221695283 * pow(tmpCalc,-0.0755148492);
         	 rgb.G = tmpCalc;
         	if (rgb.G < 0) rgb.G = 0;
         	if (rgb.G > 255) rgb.G = 255;
	}

return rgb; 


     }

struct HueHSB HueBridge::ct2hsb(long tmpKelvin, uint8_t bri) {

		RgbColor rgb = ct2rbg(tmpKelvin, bri);
		return (rgb2HUEhsb(rgb)); 
}


struct HueXYColor HueBridge::Ct2xy(long tmpKelvin, uint8_t bri) {

		RgbColor rgb = ct2rbg(tmpKelvin, bri);
		return rgb2xy(rgb); 

	} 

struct RgbColor HueBridge::XYtorgb(struct HueXYColor xy, uint8_t bri) {

 	HueHSB hsb = xy2HUEhsb(xy,bri); 
	return HUEhsb2rgb(hsb);

}


// Function to return a substring defined by a delimiter at an index
// From http://forum.arduino.cc/index.php?topic=41389.msg301116#msg301116
char* HueBridge::subStr(const char* str, char *delim, int index) {
  char *act, *sub, *ptr;
  static char copy[128]; // Length defines the maximum length of the c_string we can process
  int i;
  strcpy(copy, str); // Since strtok consumes the first arg, make a copy
  for (i = 1, act = copy; i <= index; i++, act = NULL) {
    sub = strtok_r(act, delim, &ptr);
    if (sub == NULL) break;
  }
  return sub;
}


uint8_t HueBridge::find_nextfreegroup() {

	HueGroup* currentgroup = &Groups[_nextfreegroup]; 

	if (!currentgroup->Inuse) return _nextfreegroup; 

	  for (uint8_t i = 0; i < _GroupCount; i++){
			currentgroup = &Groups[i];
			if (!currentgroup->Inuse) return i; 
	  }

	  return 0; 

}




