// matth-x/ArduinoOcpp
// Copyright Matthias Akstaller 2019 - 2022
// MIT License

#include <ArduinoOcpp/MessagesV16/StopTransaction.h>
#include <ArduinoOcpp/Core/OcppModel.h>
#include <ArduinoOcpp/Core/OperationStore.h>
#include <ArduinoOcpp/Tasks/ChargePointStatus/ChargePointStatusService.h>
#include <ArduinoOcpp/Tasks/Metering/MeteringService.h>
#include <ArduinoOcpp/Tasks/Metering/MeterValue.h>
#include <ArduinoOcpp/Tasks/Transactions/TransactionStore.h>
#include <ArduinoOcpp/Tasks/Transactions/Transaction.h>
#include <ArduinoOcpp/Debug.h>

using ArduinoOcpp::Ocpp16::StopTransaction;
using ArduinoOcpp::TransactionRPC;

StopTransaction::StopTransaction(std::shared_ptr<Transaction> transaction)
        : transaction(transaction) {

}

StopTransaction::StopTransaction(std::shared_ptr<Transaction> transaction, std::vector<std::unique_ptr<ArduinoOcpp::MeterValue>> transactionData)
        : transaction(transaction), transactionData(std::move(transactionData)) {

}

StopTransaction::StopTransaction() { }

const char* StopTransaction::getOcppOperationType(){
    return "StopTransaction";
}

void StopTransaction::initiate() {

    if (ocppModel && transaction && !transaction->getStopRpcSync().isRequested()) {
        //fill out tx data if not happened before

        auto meteringService = ocppModel->getMeteringService();
        if (transaction->getMeterStop() < 0 && meteringService) {
            auto meterStop = meteringService->readTxEnergyMeter(transaction->getConnectorId(), ReadingContext::TransactionEnd);
            if (meterStop && *meterStop) {
                transaction->setMeterStop(meterStop->toInteger());
            } else {
                AO_DBG_ERR("MeterStop undefined");
            }
        }

        if (transaction->getStopTimestamp() <= MIN_TIME) {
            transaction->setStopTimestamp(ocppModel->getOcppTime().getOcppTimestampNow());
        }

        transaction->getStopRpcSync().setRequested();

        transaction->commit();
    }
    AO_DBG_INFO("StopTransaction initiated!");
}

bool StopTransaction::initiate(StoredOperationHandler *opStore) {
    if (!opStore || !ocppModel || !transaction) {
        AO_DBG_ERR("-> legacy");
        return false; //execute legacy initiate instead
    }
    
    auto payload = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(JSON_OBJECT_SIZE(2)));
    (*payload)["connectorId"] = transaction->getConnectorId();
    (*payload)["txNr"] = transaction->getTxNr();

    opStore->setPayload(std::move(payload));

    opStore->commit();

    transaction->getStopRpcSync().setRequested();

    transaction->commit();

    return true; //don't execute legacy initiate
}

bool StopTransaction::restore(StoredOperationHandler *opStore) {
    if (!ocppModel) {
        AO_DBG_ERR("invalid state");
        return false;
    }

    if (!opStore) {
        AO_DBG_ERR("invalid argument");
        return false;
    }

    auto payload = opStore->getPayload();
    if (!payload) {
        AO_DBG_ERR("memory corruption");
        return false;
    }

    int connectorId = (*payload)["connectorId"] | -1;
    int txNr = (*payload)["txNr"] | -1;
    if (connectorId < 0 || txNr < 0) {
        AO_DBG_ERR("record incomplete");
        return false;
    }

    auto txStore = ocppModel->getTransactionStore();

    if (!txStore) {
        AO_DBG_ERR("invalid state");
        return false;
    }

    transaction = txStore->getTransaction(connectorId, txNr);
    if (!transaction) {
        AO_DBG_ERR("referential integrity violation");
        return false;
    }

    if (auto mSerivce = ocppModel->getMeteringService()) {
        if (auto txData = mSerivce->getStopTxMeterData(transaction.get())) {
            transactionData = txData->retrieveStopTxData();
        }
    }

    return true;
}

std::unique_ptr<DynamicJsonDocument> StopTransaction::createReq() {

    std::vector<std::unique_ptr<DynamicJsonDocument>> txDataJson;
    size_t txDataJson_size = 0;
    for (auto mv = transactionData.begin(); mv != transactionData.end(); mv++) {
        auto mvJson = (*mv)->toJson();
        if (!mvJson) {
            return nullptr;
        }
        txDataJson_size += mvJson->capacity();
        txDataJson.emplace_back(std::move(mvJson));
    }

    DynamicJsonDocument txDataDoc = DynamicJsonDocument(JSON_ARRAY_SIZE(txDataJson.size()) + txDataJson_size);
    for (auto mvJson = txDataJson.begin(); mvJson != txDataJson.end(); mvJson++) {
        txDataDoc.add(**mvJson);
    }

    auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(
                JSON_OBJECT_SIZE(6) + //total of 6 fields
                (IDTAG_LEN_MAX + 1) + //stop idTag
                (JSONDATE_LENGTH + 1) + //timestamp string
                (REASON_LEN_MAX + 1) + //reason string
                txDataDoc.capacity()));
    JsonObject payload = doc->to<JsonObject>();

    if (transaction->getStopIdTag() && *transaction->getStopIdTag()) {
        payload["idTag"] = (char*) transaction->getStopIdTag();
    }

    if (transaction->isMeterStopDefined()) {
        payload["meterStop"] = transaction->getMeterStop();
    }

    if (transaction->getStopTimestamp() > MIN_TIME) {
        char timestamp [JSONDATE_LENGTH + 1] = {'\0'};
        transaction->getStopTimestamp().toJsonString(timestamp, JSONDATE_LENGTH + 1);
        payload["timestamp"] = timestamp;
    }

    payload["transactionId"] = transaction->getTransactionId();
    
    if (transaction->getStopReason() && *transaction->getStopReason()) {
        payload["reason"] = (char*) transaction->getStopReason();
    }

    if (!transactionData.empty()) {
        payload["transactionData"] = txDataDoc;
    }

    return doc;
}

void StopTransaction::processConf(JsonObject payload) {

    if (transaction) {
        transaction->getStopRpcSync().confirm();
        transaction->commit();
    }

    AO_DBG_INFO("Request has been accepted!");
}

bool StopTransaction::processErr(const char *code, const char *description, JsonObject details) {

    if (transaction) {
        transaction->getStopRpcSync().confirm(); //no retry behavior for now; consider data "arrived" at server
        transaction->commit();
    }

    AO_DBG_ERR("Server error, data loss!");

    return false;
}

void StopTransaction::processReq(JsonObject payload) {
    /**
     * Ignore Contents of this Req-message, because this is for debug purposes only
     */
}

std::unique_ptr<DynamicJsonDocument> StopTransaction::createConf(){
    auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(2 * JSON_OBJECT_SIZE(1)));
    JsonObject payload = doc->to<JsonObject>();

    JsonObject idTagInfo = payload.createNestedObject("idTagInfo");
    idTagInfo["status"] = "Accepted";

    return doc;
}
