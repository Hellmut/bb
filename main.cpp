// async_subscribe.cpp
//
// This is a Paho MQTT C++ client, sample application.
//
// This application is an MQTT subscriber using the C++ asynchronous client
// interface, employing callbacks to receive messages and status updates.
//
// The sample demonstrates:
//  - Connecting to an MQTT server/broker.
//  - Subscribing to a topic
//  - Receiving messages through the callback API
//  - Receiving network disconnect updates and attempting manual reconnects.
//  - Using a "clean session" and manually re-subscribing to topics on
//    reconnect.
//

/*******************************************************************************
 * Copyright (c) 2013-2017 Frank Pagliughi <fpagliughi@mindspring.com>
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Frank Pagliughi - initial implementation and documentation
 *******************************************************************************/

#include <iostream>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>
#include "mqtt/async_client.h"

#include "secrets.h"



#ifdef __cplusplus
extern "C"{
#endif 

#include <mpsse.h>

#ifdef __cplusplus
}
#endif


const int	QOS = 1;
const int	N_RETRY_ATTEMPTS = 5;


std::condition_variable commandCv;
std::mutex commandMutex;
std::mutex engineMutex;

bool stopApp = false;

class Platform {
 public:
  static Platform& Instance() {
    // Since it's a static variable, if the class has already been created,
    // it won't be created again.
    // And it **is** thread-safe in C++11.
    static Platform myInstance;

    // Return a reference to our instance.
    return myInstance;
  }

  // delete copy and move constructors and assign operators
  Platform(Platform const&) = delete;             // Copy construct
  Platform(Platform&&) = delete;                  // Move construct
  Platform& operator=(Platform const&) = delete;  // Copy assign
  Platform& operator=(Platform &&) = delete;      // Move assign

  // Any other public methods.
  void setEnginesCommand(std::string command){
	  std::lock_guard<std::mutex> lock(commandMutex);
	  enginesCommandLast = enginesCommandNow;
	  enginesCommandNow = command;
	  std::cout<<"Engine command: "<<command<<" set."<<std::endl;
  }

  // Any other public methods.
  void setEngines(std::string command){
	  //std::lock_guard<std::mutex> lock(engineMutex);
	  enginesNow = command;
	  std::cout<<"Engine Now: "<<command<<" set."<<std::endl;
  }

  std::string getEngineCommandNow(){
	  std::lock_guard<std::mutex> lock(commandMutex);
	  return enginesCommandNow;
  }

  std::string getEngineCommandLast(){
	  std::lock_guard<std::mutex> lock(commandMutex);
	  return enginesCommandLast;
  }

  void engineCommandArrived(bool arrived) {
	  std::lock_guard<std::mutex> lock(commandMutex);
	  engineCommandFlag = arrived;
  }

  bool getEngineCommandFlag(void) {
	  std::lock_guard<std::mutex> lock(commandMutex);
      return engineCommandFlag;
  }
 protected:
  Platform() : enginesCommandLast("OO"), enginesCommandNow("OO"), enginesNow("OO"), engineCommandFlag(false) {
    // Constructor code goes here.
  }

  ~Platform() {
    // Destructor code goes here.
  }

 // And any other protected methods.

 private:
    std::string enginesCommandNow;
	std::string enginesCommandLast;
	std::string enginesNow;
	bool engineCommandFlag;
};




/////////////////////////////////////////////////////////////////////////////

// Callbacks for the success or failures of requested actions.
// This could be used to initiate further action, but here we just log the
// results to the console.

class action_listener : public virtual mqtt::iaction_listener
{
	std::string name_;

	void on_failure(const mqtt::token& tok) override {
		std::cout << name_ << " failure";
		if (tok.get_message_id() != 0)
			std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
		std::cout << std::endl;
	}

	void on_success(const mqtt::token& tok) override {
		std::cout << name_ << " success";
		if (tok.get_message_id() != 0)
			std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
		auto top = tok.get_topics();
		if (top && !top->empty())
			std::cout << "\ttoken topic: '" << (*top)[0] << "', ..." << std::endl;
		std::cout << std::endl;
	}

public:
	action_listener(const std::string& name) : name_(name) {}
};

/////////////////////////////////////////////////////////////////////////////

/**
 * Local callback & listener class for use with the client connection.
 * This is primarily intended to receive messages, but it will also monitor
 * the connection to the broker. If the connection is lost, it will attempt
 * to restore the connection and re-subscribe to the topic.
 */
class callback : public virtual mqtt::callback,
					public virtual mqtt::iaction_listener

{
	// Counter for the number of connection retries
	int nretry_;
	// The MQTT client
	mqtt::async_client& cli_;
	// Options to use if we need to reconnect
	mqtt::connect_options& connOpts_;
	// An action listener to display the result of actions.
	action_listener subListener_;

	// This deomonstrates manually reconnecting to the broker by calling
	// connect() again. This is a possibility for an application that keeps
	// a copy of it's original connect_options, or if the app wants to
	// reconnect with different options.
	// Another way this can be done manually, if using the same options, is
	// to just call the async_client::reconnect() method.
	void reconnect() {
		std::this_thread::sleep_for(std::chrono::milliseconds(2500));
		try {
			cli_.connect(connOpts_, nullptr, *this);
		}
		catch (const mqtt::exception& exc) {
			std::cerr << "Error: " << exc.what() << std::endl;
			exit(1);
		}
	}

	// Re-connection failure
	void on_failure(const mqtt::token& tok) override {
		std::cout << "Connection attempt failed" << std::endl;
		if (++nretry_ > N_RETRY_ATTEMPTS)
			exit(1);
		reconnect();
	}

	// (Re)connection success
	// Either this or connected() can be used for callbacks.
	void on_success(const mqtt::token& tok) override {}

	// (Re)connection success
	void connected(const std::string& cause) override {
		std::cout << "\nConnection success" << std::endl;
		std::cout << "\nSubscribing to topic '" << TOPIC << "'\n"
			<< "\tfor client " << CLIENT_ID
			<< " using QoS" << QOS << "\n"
			<< "\nPress Q<Enter> to quit\n" << std::endl;

		cli_.subscribe(TOPIC, QOS, nullptr, subListener_);
	}

	// Callback for when the connection is lost.
	// This will initiate the attempt to manually reconnect.
	void connection_lost(const std::string& cause) override {
		std::cout << "\nConnection lost" << std::endl;
		if (!cause.empty())
			std::cout << "\tcause: " << cause << std::endl;

		std::cout << "Reconnecting..." << std::endl;
		nretry_ = 0;
		reconnect();
	}

	// Callback for when a message arrives.
	void message_arrived(mqtt::const_message_ptr msg) override {
		std::cout << "Message arrived" << std::endl;
		std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
		std::cout << "\tpayload: '" << msg->to_string() << "'\n" << std::endl;
        
		Platform::Instance().setEnginesCommand(msg->to_string());
		Platform::Instance().engineCommandArrived(true);
		commandCv.notify_all();
	}

	void delivery_complete(mqtt::delivery_token_ptr token) override {}

public:
	callback(mqtt::async_client& cli, mqtt::connect_options& connOpts)
				: nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription") {}
};

/////////////////////////////////////////////////////////////////////////////




void gpio(){
	struct mpsse_context *io = NULL;
	int i = 0, retval = EXIT_FAILURE;

	io = MPSSE(GPIO, 0, 0);
	
	if(io && io->open)
	{
		for(i=0; i<10; i++)
		{
			PinHigh(io, GPIOL0);
			printf("GPIOL0 State: %d\n", PinState(io, GPIOL0, -1));
			sleep(1);
			
			PinLow(io, GPIOL0);
			printf("GPIOL0 State: %d\n", PinState(io, GPIOL0, -1));
			sleep(1);
		}
	
		retval = EXIT_SUCCESS;
	}
	else
	{
		printf("Failed to open MPSSE: %s\n", ErrorString(io));
	}
		
	Close(io);


}


void gpioControl() {
	std::cout << "Enter gpioControl ."<<std::endl;
		struct mpsse_context *io = NULL;
	int i = 0, retval = EXIT_FAILURE;

	io = MPSSE(GPIO, 0, 0);
	
	if(io && io->open)
	{
		while(!stopApp) {
			std::unique_lock<std::mutex> lock(engineMutex);
    		if(commandCv.wait_for(lock, std::chrono::milliseconds(250), []{return Platform::Instance().getEngineCommandFlag();})) { 
        		std::cout << "gpio set new command."<<std::endl;
				Platform::Instance().engineCommandArrived(false);
				std::string command = Platform::Instance().getEngineCommandNow();
				 
                if(!command.compare("OO")) {
					PinLow(io, GPIOH0);
					PinLow(io, GPIOH1);
					PinLow(io, GPIOH2);
					PinLow(io, GPIOH3);
				} else if (!command.compare("FF")) {
                    PinLow(io, GPIOH0);
					PinLow(io, GPIOH2);
					PinHigh(io, GPIOH1);
					PinHigh(io, GPIOH3);
				} else if (!command.compare("FO")) {
                    PinLow(io, GPIOH0);
					PinLow(io, GPIOH2);
					PinLow(io, GPIOH3);
					PinHigh(io, GPIOH1);
				} else if (!command.compare("FR")) {
                    PinLow(io, GPIOH0);
					PinLow(io, GPIOH3);
					PinHigh(io, GPIOH1);
					PinHigh(io, GPIOH2);
				} else if (!command.compare("OR")) {
                    PinLow(io, GPIOH0);
					PinLow(io, GPIOH1);
					PinLow(io, GPIOH3);
					PinHigh(io, GPIOH2);
				} else if (!command.compare("RR")) {
					PinLow(io, GPIOH1);
					PinLow(io, GPIOH3);
                    PinHigh(io, GPIOH0);
					PinHigh(io, GPIOH2);
				} else if (!command.compare("RO")) {
					PinLow(io, GPIOH2);
					PinLow(io, GPIOH3);
					PinLow(io, GPIOH1);
					PinHigh(io, GPIOH0);
				} else if (!command.compare("RF")) {
					PinLow(io, GPIOH1);
					PinLow(io, GPIOH2);
                    PinHigh(io, GPIOH0);
					PinHigh(io, GPIOH3);
				} else if (!command.compare("OF")) {
                    PinLow(io, GPIOH0);
					PinLow(io, GPIOH1);
					PinLow(io, GPIOH2);
					PinHigh(io, GPIOH3);
				} else {
					std::cout<< "Unkown command" <<std::endl;
					PinLow(io, GPIOH0);
					PinLow(io, GPIOH1);
					PinLow(io, GPIOH2);
					PinLow(io, GPIOH3);
				} 
				
				Platform::Instance().setEngines(command);
			} else {
        		std::cout << "gpio timed out."<<std::endl;
				Platform::Instance().engineCommandArrived(false);
				PinLow(io, GPIOH0);
				PinLow(io, GPIOH1);
				PinLow(io, GPIOH2);
				PinLow(io, GPIOH3);
				Platform::Instance().setEngines("OO");
			}
		}
		Close(io);
	} else {
		std::cerr<<"Cannot open EMPSEE"<<std::endl;
	}
}

int main(int argc, char* argv[])
{

    std::thread gpioController(gpioControl);

	mqtt::connect_options connOpts;
	connOpts.set_keep_alive_interval(20);
	connOpts.set_clean_session(true);
	connOpts.set_user_name(USER_NAME);
	connOpts.set_password(PASSWORD);

	mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

	callback cb(client, connOpts);
	client.set_callback(cb);

	// Start the connection.
	// When completed, the callback will subscribe to topic.

	try {
		std::cout << "Connecting to the MQTT server..."<<std::endl << std::flush;
		client.connect(connOpts, nullptr, cb);
	}
	catch (const mqtt::exception&) {
		std::cerr << "\nERROR: Unable to connect to MQTT server: '"
			<< SERVER_ADDRESS << "'" << std::endl;
		return 1;
	}

	// Just block till user tells us to quit.

	while (std::tolower(std::cin.get()) != 'q')
		;
    stopApp = true;
	// Disconnect
    
	try {
		std::cout << "\nDisconnecting from the MQTT server..."<<std::endl << std::flush;
		client.disconnect()->wait();
		std::cout << "OK" << std::endl;
	}
	catch (const mqtt::exception& exc) {
		std::cerr << exc.what() << std::endl;
		return 1;
	}
    gpioController.join();
 	return 0;
}



//GPIO4 - Left Back
//GPIO5 - Let forward
//GPIO6 - Right Back
//GPIO7 - Right forward