//
// implementing android app lifecycle in logger
//

#include "LooperEvent.h"
#include "helpers/Logger.h"

#define STARTING "Starting Event Loop"
#define EXIT "Exiting Event Loop"

EventLifecycle::EventLifecycle
        (android_app *_app, ActivityHandler & _activityHandler, InputHandler& inputHandler) :
        _pApplication(_app),
        _ActivityHandler(_activityHandler),
        _Quit(false), _Enabled(false),
        _InputHandler(inputHandler),
        _SensorEventQueue(NULL),
        _Accelerometer(NULL),
        _SensorManager(NULL),
        Sensor_PollSource() {
    _pApplication->userData = this;
    _pApplication->onAppCmd = callback_appEvent;
    _pApplication->onInputEvent = callbackInput;
}

void EventLifecycle::run() {
    int32_t result, events;
    android_poll_source *source;
    //lock current code
   // app_dummy(); //it is not necessary

    Logger::info(STARTING);
    while (true) {
        // loop and get events
        while ((result = ALooper_pollAll(_Enabled ? 0 : -1, NULL, &events, (void**) &source)) >= 0) {
            //events for get
            if (source != NULL) {
                source->process(_pApplication, source);
            }
            //if app closed
            if (_pApplication->destroyRequested) {
                Logger::info(EXIT);
                return;
            }
        }

        if (_Enabled && !_Quit) {
            if (_ActivityHandler.onStep() != STATUS_OK) {
                _Quit = true;
                ANativeActivity_finish(_pApplication->activity);
            }
        }
    }
}

void EventLifecycle::activate() {
    //activate visual component only if window enabled
    if (!_Enabled && _pApplication->window != NULL) {
        Sensor_PollSource.id = LOOPER_ID_USER;
        Sensor_PollSource.app = _pApplication;
        Sensor_PollSource.process = callbackSensor;
        _SensorManager = ASensorManager_getInstance();
        if (_SensorManager != nullptr) {
            _SensorEventQueue = ASensorManager_createEventQueue(_SensorManager,
            _pApplication->looper, LOOPER_ID_USER, nullptr, &Sensor_PollSource);
            if (_SensorEventQueue == nullptr) goto ERROR;
        }
        activateAccelerometer();
        _Quit = false; _Enabled = true;
        if (_ActivityHandler.onActive() != STATUS_OK) {
            goto ERROR;
        }
    }
    return;
    ERROR: {
    _Quit = true;
    deactivate();
    ANativeActivity_finish(_pApplication->activity);
}
}

void EventLifecycle::deactivate() {
    if (_Enabled) {
        deactivateAccelerometer();
        if (_SensorEventQueue != nullptr) {
            ASensorManager_destroyEventQueue(_SensorManager, _SensorEventQueue);
            _SensorEventQueue = nullptr;
        }
        _SensorManager = nullptr;
        _ActivityHandler.onDeactivate();
        _Enabled = false;
    }
}

void EventLifecycle::callback_appEvent(android_app * _app, int32_t _command) {
    //get ptr for our class from structure
    EventLifecycle& _eventLoop = *(static_cast<EventLifecycle *>(_app->userData));
    _eventLoop.processAppEvent(_command);
}

void EventLifecycle::processAppEvent(int32_t _command) {
    switch (_command) {
        case APP_CMD_CONFIG_CHANGED:
            _ActivityHandler.onConfigurationChanged();
            break;
        case APP_CMD_INIT_WINDOW:
            _ActivityHandler.onCreateWindow();
            break;
        case APP_CMD_DESTROY:
            _ActivityHandler.onDestroy();
            break;
        case APP_CMD_GAINED_FOCUS:
            activate();
            _ActivityHandler.onGainFocus();
            break;
        case APP_CMD_LOST_FOCUS:
            _ActivityHandler.onLostFocus();
            deactivate();
            break;
        case APP_CMD_LOW_MEMORY:
            _ActivityHandler.onLowMemory();
            break;
        case APP_CMD_PAUSE:
            _ActivityHandler.onPause();
            deactivate();
            break;
        case APP_CMD_RESUME:
            _ActivityHandler.onResume();
            break;
        case APP_CMD_SAVE_STATE:
            _ActivityHandler.onSaveInstanceState(&_pApplication->savedState,
                                                 &_pApplication->savedStateSize);
            break;
        case APP_CMD_START:
            _ActivityHandler.onStart();
            break;
        case APP_CMD_STOP:
            _ActivityHandler.onStop();
            break;
        case APP_CMD_TERM_WINDOW:
            _ActivityHandler.onDestroyWindow();
            deactivate();
            break;
        default:
            break;
    }
}

void EventLifecycle::callbackSensor(android_app * pApp, android_poll_source * pAndroidPollSrc) {
    EventLifecycle& lifecycle = *static_cast<EventLifecycle*>(pApp->userData);
    lifecycle.processEventSensor();
}

void EventLifecycle::processEventSensor() {
    ASensorEvent event;
    if (!_Enabled) return;
    while (ASensorEventQueue_getEvents(_SensorEventQueue, &event, 1) > 0) {
        switch (event.type) {
            case ASENSOR_TYPE_ACCELEROMETER:
                _InputHandler.onAccelerometerEvent(&event);
                break;
        }
    }
}


int32_t EventLifecycle::callbackInput(android_app * app, AInputEvent * event) {
    EventLifecycle& eventLifecycle = *(EventLifecycle*) app->userData;
    return eventLifecycle.processInputEvent(event);
}

int32_t EventLifecycle::processInputEvent(AInputEvent * inputEvent) {
    if (!_Enabled) return 0;

    int32_t eventType = AInputEvent_getType(inputEvent);
    switch (eventType) {
        case AINPUT_EVENT_TYPE_MOTION:
            switch (AInputEvent_getSource(inputEvent)) {
                case AINPUT_SOURCE_TOUCHSCREEN:
                    return _InputHandler.onTouchEvent(inputEvent);
            }
            break;
    }
    return 0;
}

void EventLifecycle::activateAccelerometer() {
    _Accelerometer = ASensorManager_getDefaultSensor(_SensorManager, ASENSOR_TYPE_ACCELEROMETER);
    if (_Accelerometer != nullptr) {
        if (ASensorEventQueue_enableSensor(_SensorEventQueue, _Accelerometer) < 0) {
            Logger::error("Error while activate accelerometer");
            return;
        }
        int32_t minDelay = ASensor_getMinDelay(_Accelerometer);
        if (ASensorEventQueue_setEventRate(_SensorEventQueue, _Accelerometer, minDelay) < 0) {
            Logger::error("Could not set accelerometer rate");
        }
    } else {
        Logger::error("Error while activate accelerometer, can not find any acc - sensor");
    }

}

void EventLifecycle::deactivateAccelerometer() {
    if (_Accelerometer != nullptr) {
        if (ASensorEventQueue_disableSensor(_SensorEventQueue, _Accelerometer) < 0) {
            Logger::error("Error while deactivating sensor");
        }
        _Accelerometer = nullptr;
    }
}