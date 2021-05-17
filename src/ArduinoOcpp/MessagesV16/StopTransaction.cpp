// matth-x/ESP8266-OCPP
// Copyright Matthias Akstaller 2019 - 2021
// MIT License

#include <ArduinoOcpp/MessagesV16/StopTransaction.h>
#include <ArduinoOcpp/Core/OcppEngine.h>
#include <ArduinoOcpp/Tasks/Metering/MeteringService.h>
#include <Variants.h>

using ArduinoOcpp::Ocpp16::StopTransaction;

StopTransaction::StopTransaction() {

}

StopTransaction::StopTransaction(int connectorId) : connectorId(connectorId) {

}

const char* StopTransaction::getOcppOperationType(){
    return "StopTransaction";
}

DynamicJsonDocument* StopTransaction::createReq() {

    DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(4) + (JSONDATE_LENGTH + 1));
    JsonObject payload = doc->to<JsonObject>();

    float meterStop = 0.0f;
    if (getMeteringService() != NULL) {
        meterStop = getMeteringService()->readEnergyActiveImportRegister(connectorId);
    }

    payload["meterStop"] = meterStop; //TODO meterStart is required to be in Wh, but measuring unit is probably inconsistent in implementation
    char timestamp[JSONDATE_LENGTH + 1] = {'\0'};
    getJsonDateStringFromSystemTime(timestamp, JSONDATE_LENGTH);
    payload["timestamp"] = timestamp;

    ConnectorStatus *connector = getConnectorStatus(connectorId);
    if (connector != NULL) {
        if (connector->getTransactionId() >= 0) {
            //Connector is in a transaction (transactionId > 0) or in an undefinded state (transactionId == 0).

            //End the charging session in each module.

            transactionId = connector->getTransactionId();

            connector->stopEnergyOffer();
            connector->stopTransaction();
            connector->unauthorize();

            SmartChargingService *scService = getSmartChargingService();
            if (scService != NULL){
                scService->endChargingNow();
            }
        } // else: Connector says that is is not involved in a transaction (anymore). It has been ended before, so do nothing
    }

    payload["transactionId"] = transactionId;

    return doc;
}

void StopTransaction::processConf(JsonObject payload) {

    //no need to process anything here

    if (DEBUG_OUT) Serial.print(F("[StopTransaction] Request has been accepted!\n"));
    
}


void StopTransaction::processReq(JsonObject payload) {
    /**
     * Ignore Contents of this Req-message, because this is for debug purposes only
     */
}

DynamicJsonDocument* StopTransaction::createConf(){
    DynamicJsonDocument* doc = new DynamicJsonDocument(2 * JSON_OBJECT_SIZE(1));
    JsonObject payload = doc->to<JsonObject>();

    JsonObject idTagInfo = payload.createNestedObject("idTagInfo");
    idTagInfo["status"] = "Accepted";

    return doc;
}
