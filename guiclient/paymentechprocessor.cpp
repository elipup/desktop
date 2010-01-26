/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2010 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#include <QSqlError>

#include <currcluster.h>

#include "guiclient.h"
#include "paymentechprocessor.h"

#define DEBUG false

PaymentechProcessor::PaymentechProcessor() : CreditCardProcessor()
{
  _defaultLivePort   = 443;
  _defaultLiveServer = "https://netconnectka1.paymentech.net/NetConnect/controller";
  _defaultTestPort   = 443;
  _defaultTestServer = "https://netconnectkavar1.paymentech.net/NetConnect/controller";

  _msgHash.insert(-200, tr("A Login is required."));
  _msgHash.insert(-201, tr("A Password is required."));
  _msgHash.insert(-202, tr("A Division Number must be no more than 10 characters long."));
  _msgHash.insert(-205, tr("The response from the Gateway appears to be "
			   "incorrectly formatted (could not find field %1 "
			   "as there are only %2 fields present)."));
  _msgHash.insert(-206, tr("The response from the Gateway failed the MD5 "
			   "security check."));
  _msgHash.insert( 206, tr("The response from the Gateway has failed the MD5 "
			   "security check but will be processed anyway."));
  _msgHash.insert(-207, tr("The Gateway returned the following error: %1"));
  _msgHash.insert(-209, tr("The selected credit card is not a know type for Paymentech."));
}

int PaymentechProcessor::buildCommon(QString & pordernum, const int pccardid, const int pcvv, const double pamount, const int pcurrid, QString &prequest, QString pordertype)
{
  XSqlQuery anq;
  anq.prepare(
    "SELECT ccard_active,"
    "  formatbytea(decrypt(setbytea(ccard_number),   setbytea(:key),'bf')) AS ccard_number,"
    "  formatccnumber(decrypt(setbytea(ccard_number),setbytea(:key),'bf')) AS ccard_number_x,"
    "  formatbytea(decrypt(setbytea(ccard_name),     setbytea(:key),'bf')) AS ccard_name,"
    "  formatbytea(decrypt(setbytea(ccard_address1), setbytea(:key),'bf')) AS ccard_address1,"
    "  formatbytea(decrypt(setbytea(ccard_address2), setbytea(:key),'bf')) AS ccard_address2,"
    "  formatbytea(decrypt(setbytea(ccard_city),     setbytea(:key),'bf')) AS ccard_city,"
    "  formatbytea(decrypt(setbytea(ccard_state),    setbytea(:key),'bf')) AS ccard_state,"
    "  formatbytea(decrypt(setbytea(ccard_zip),      setbytea(:key),'bf')) AS ccard_zip,"
    "  formatbytea(decrypt(setbytea(ccard_country),  setbytea(:key),'bf')) AS ccard_country,"
    "  formatbytea(decrypt(setbytea(ccard_month_expired),setbytea(:key),'bf')) AS ccard_month_expired,"
    "  formatbytea(decrypt(setbytea(ccard_year_expired),setbytea(:key), 'bf')) AS ccard_year_expired,"
    "  ccard_type,"
    "  custinfo.* "
    "  FROM ccard, custinfo "
    "WHERE ((ccard_id=:ccardid)"
    "  AND  (ccard_cust_id=cust_id));");
  anq.bindValue(":ccardid", pccardid);
  anq.bindValue(":key",     omfgThis->_key);
  anq.exec();

  if (anq.first())
  {
    if (!anq.value("ccard_active").toBool())
    {
      _errorMsg = errorMsg(-10);
      return -10;
    }
  }
  else if (anq.lastError().type() != QSqlError::NoError)
  {
    _errorMsg = anq.lastError().databaseText();
    return -1;
  }
  else
  {
    _errorMsg = errorMsg(-17).arg(pccardid);
    return -17;
  }

  _extraHeaders.clear();
  _extraHeaders.append(qMakePair(QString("Stateless-Transaction"), QString("true")));
  _extraHeaders.append(qMakePair(QString("Auth-MID"), QString(_metricsenc->value("CCPTDivisionNumber").rightJustified(10, '0', true))));
  _extraHeaders.append(qMakePair(QString("Auth-User"), QString(_metricsenc->value("CCLogin"))));
  _extraHeaders.append(qMakePair(QString("Auth-Password"), QString(_metricsenc->value("CCPassword"))));
  _extraHeaders.append(qMakePair(QString("Content-type"), QString("SALEM05210/SLM")));

  prequest = "P74V";
  prequest += pordernum.leftJustified(22, ' ', true); // TODO: should we check to make sure isn't empty?

  QString ccardType = anq.value("ccard_type").toString();
  if("V" == ccardType) // Visa
    ccardType = "VI";
  else if("M" == ccardType) // Master Card
    ccardType = "MC";
  else if("A" == ccardType) // American Express
    ccardType = "AX";
  else if("D" == ccardType) // Discover
    ccardType = "DI";
  else if("P" == ccardType) // PayPal
    ccardType = "PY";
  else
  {
    _errorMsg = errorMsg(-209);
    return -209;
  }
  prequest += ccardType;

  prequest += anq.value("ccard_number").toString().leftJustified(19, ' ', true);

  QString work_month;
  work_month.setNum(anq.value("ccard_month_expired").toDouble());
  if (work_month.length() == 1)
    work_month = "0" + work_month;
  prequest += work_month + anq.value("ccard_year_expired").toString().right(2);

  prequest += _metricsenc->value("CCPTDivisionNumber").rightJustified(10, '0', true);

  double shiftedAmt = pamount * 100.0;
  int amount = (int)shiftedAmt;
  prequest += QString::number(amount).rightJustified(12, '0', true);

  // TODO: this needs to be changed to support non-us
  prequest += "840"; // CurrencyCode: U.S. Dollars

  prequest += "1"; // TransactionType: 1 - single trans over mail/phone card holder not present

  prequest += "    "; // EncryptionFlag, PaymentIndicator: both optional not using

  prequest += pordertype; // ActionCode 2 digit

  prequest += " "; // Reserved

  // Bill To Address Information
  prequest += "AB";
  prequest += "               "; // TelephoneType, TelephoneNumber (1,14, Optional)
  prequest += anq.value("ccard_name").toString().leftJustified(30, ' ', true);
  prequest += anq.value("ccard_address1").toString().leftJustified(30, ' ', true);
  prequest += anq.value("ccard_address2").toString().leftJustified(28, ' ', true);

  prequest += "  "; // CountryCode (2, optional) ccard_country // TODO: figure out how to handle this appropriately if we want to at all
  prequest += anq.value("ccard_city").toString().leftJustified(20, ' ', true);
  prequest += "  "; // State (2, optional) ccard_state // TODO: figure out how to handle this appropriately if we want to at all
  prequest += anq.value("ccard_zip").toString().leftJustified(10, ' ', true);

/* TODO: should probably do something about this in relation to above with the CurrencyCode
  anq.prepare("SELECT curr_abbr FROM curr_symbol WHERE (curr_id=:currid);");
  anq.bindValue(":currid", pcurrid);
  anq.exec();
  if (anq.first())
  {
    APPENDFIELD(prequest, "x_currency_code", anq.value("curr_abbr").toString());
  }
  else if (anq.lastError().type() != QSqlError::NoError)
  {
    _errorMsg = anq.lastError().databaseText();
    return -1;
  }
  else
  {
    _errorMsg = errorMsg(-17).arg(pccardid);
    return -17;
  }
*/

  if (pcvv > 0)
  {
    prequest += "FR";
    prequest += "1";
    prequest += QString::number(amount).leftJustified(4, ' ', true);
  }


  _extraHeaders.append(qMakePair(QString("Content-Length"), QString("%1").arg(prequest.size())));

  if (DEBUG)
    qDebug("Paymentech:buildCommon built %s\n", prequest.toAscii().data());
  return 0;
}

int  PaymentechProcessor::doAuthorize(const int pccardid, const int pcvv, const double pamount, const double ptax, const bool ptaxexempt, const double pfreight, const double pduty, const int pcurrid, QString& pneworder, QString& preforder, int &pccpayid, ParameterList &pparams)
{
  if (DEBUG)
    qDebug("Paymentech:doAuthorize(%d, %d, %f, %f, %d, %f, %f, %d, %s, %s, %d)",
	   pccardid, pcvv, pamount, ptax, ptaxexempt,  pfreight,  pduty, pcurrid,
	   pneworder.toAscii().data(), preforder.toAscii().data(), pccpayid);

  int    returnValue = 0;
  double amount  = pamount;
  double tax     = ptax;
  double freight = pfreight;
  double duty    = pduty;
  int    currid  = pcurrid;

  QString request;
  returnValue = buildCommon(pneworder, pccardid, pcvv, amount, currid, request, "RC");
  if (returnValue != 0)
    return returnValue;

  QString response;

  returnValue = sendViaHTTP(request, response);
  if (returnValue < 0)
    return returnValue;

  returnValue = handleResponse(response, pccardid, "A", amount, currid,
			       pneworder, preforder, pccpayid, pparams);

  return returnValue;
}

int  PaymentechProcessor::doCharge(const int pccardid, const int pcvv, const double pamount, const double ptax, const bool ptaxexempt, const double pfreight, const double pduty, const int pcurrid, QString& pneworder, QString& preforder, int &pccpayid, ParameterList &pparams)
{
  if (DEBUG)
    qDebug("Paymentech:doCharge(%d, %d, %f, %f, %d, %f, %f, %d, %s, %s, %d)",
	   pccardid, pcvv, pamount,  ptax, ptaxexempt,  pfreight,  pduty, pcurrid,
	   pneworder.toAscii().data(), preforder.toAscii().data(), pccpayid);

  int    returnValue = 0;
  double amount  = pamount;
  double tax     = ptax;
  double freight = pfreight;
  double duty    = pduty;
  int    currid  = pcurrid;

  QString request;

  returnValue = buildCommon(pneworder, pccardid, pcvv, amount, currid, request, "RP");
  if (returnValue !=  0)
    return returnValue;

  QString response;

  returnValue = sendViaHTTP(request, response);
  if (returnValue < 0)
    return returnValue;

  returnValue = handleResponse(response, pccardid, "C", amount, currid,
			       pneworder, preforder, pccpayid, pparams);

  return returnValue;
}

int PaymentechProcessor::doChargePreauthorized(const int pccardid, const int pcvv, const double pamount, const int pcurrid, QString &pneworder, QString &preforder, int &pccpayid, ParameterList &pparams)
{
  if (DEBUG)
    qDebug("Paymentech:doChargePreauthorized(%d, %d, %f, %d, %s, %s, %d)",
	   pccardid, pcvv, pamount,  pcurrid,
	   pneworder.toAscii().data(), preforder.toAscii().data(), pccpayid);

  int    returnValue = 0;
  double amount  = pamount;
  int    currid  = pcurrid;

  QString request;

  returnValue = buildCommon(pneworder, pccardid, pcvv, amount, currid, request, "AU");
  if (returnValue !=  0)
    return returnValue;

  QString response;

  returnValue = sendViaHTTP(request, response);
  if (returnValue < 0)
    return returnValue;

  returnValue = handleResponse(response, pccardid, "CP", amount, currid,
			       pneworder, preforder, pccpayid, pparams);

  return returnValue;
}

int PaymentechProcessor::doCredit(const int pccardid, const int pcvv, const double pamount, const double ptax, const bool ptaxexempt, const double pfreight, const double pduty, const int pcurrid, QString &pneworder, QString &preforder, int &pccpayid, ParameterList &pparams)
{
  if (DEBUG)
    qDebug("Paymentech:doCredit(%d, %d, %f, %f, %d, %f, %f, %d, %s, %s, %d)",
	   pccardid, pcvv, pamount, ptax, ptaxexempt,  pfreight,  pduty, pcurrid,
	   pneworder.toAscii().data(), preforder.toAscii().data(), pccpayid);

  int    returnValue = 0;
  double amount  = pamount;
  double tax     = ptax;
  double freight = pfreight;
  double duty    = pduty;
  int    currid  = pcurrid;

  QString request;

  returnValue = buildCommon(pneworder, pccardid, pcvv, amount, currid, request, "RF");
  if (returnValue !=  0)
    return returnValue;

  QString response;
  returnValue = sendViaHTTP(request, response);
  if (returnValue < 0)
    return returnValue;

  returnValue = handleResponse(response, pccardid, "R", amount, currid,
			       pneworder, preforder, pccpayid, pparams);

  return returnValue;
}

int PaymentechProcessor::doVoidPrevious(const int pccardid, const int pcvv, const double pamount, const int pcurrid, QString &pneworder, QString &preforder, QString &papproval, int &pccpayid, ParameterList &pparams)
{
  if (DEBUG)
    qDebug("Paymentech:doVoidPrevious(%d, %d, %f, %d, %s, %s, %s, %d)",
	   pccardid, pcvv, pamount, pcurrid,
	   pneworder.toAscii().data(), preforder.toAscii().data(),
	   papproval.toAscii().data(), pccpayid);

  QString tmpErrorMsg = _errorMsg;

  int    returnValue = 0;
  double amount = pamount;
  int    currid = pcurrid;

  QString request;

  returnValue = buildCommon(pneworder, pccardid, pcvv, amount, currid, request, "AR");
  if (returnValue != 0)
    return returnValue;

  QString response;

  returnValue = sendViaHTTP(request, response);
  _errorMsg = tmpErrorMsg;
  if (returnValue < 0)
    return returnValue;

  returnValue = handleResponse(response, pccardid, "V", amount, currid,
			       pneworder, preforder, pccpayid, pparams);
  if (! tmpErrorMsg.isEmpty())
    _errorMsg = tmpErrorMsg;
  return returnValue;
}

int PaymentechProcessor::handleResponse(const QString &presponse, const int pccardid, const QString &ptype, const double pamount, const int pcurrid, QString &pneworder, QString &preforder, int &pccpayid, ParameterList &pparams)
{
  if (DEBUG)
    qDebug("Paymentech::handleResponse(%s, %d, %s, %f, %d, %s, %d, pparams)",
	   presponse.toAscii().data(), pccardid,
	   ptype.toAscii().data(), pamount, pcurrid,
	   preforder.toAscii().data(), pccpayid);

  int returnValue = 0;

  QString r_approved;
  QString r_avs;
  QString r_code;
  QString r_cvv;
  QString r_error;
  QString r_message;
  QString r_ordernum;
  QString r_reason;     // not stored
  QString r_ref;
  QString r_shipping;
  QString r_tax;

  QString status;

  QString r_response;
  r_response = presponse.mid(27, 3).trimmed();
  int i_response = r_response.toInt();
  if ((i_response >= 100 && i_response < 200) || i_response == 704)
    r_approved = "APPROVED";
  else if((i_response >= 200 && i_response < 300 && i_response != 260) || (i_response >= 740 && i_response <= 768))
    r_approved = "ERROR"; // REJECTED
  else
    r_approved = "DECLINED";

  r_reason = r_response;

  r_message = "not yet implemented"; // TODO: come up with a way to populate this

  r_code = presponse.mid(36, 6).trimmed();
  r_avs = presponse.mid(42, 2).trimmed();
  r_ordernum = presponse.mid(5, 22).trimmed();
  r_cvv = presponse.mid(44, 1).trimmed();

  if (r_approved == "APPROVED")
  {
    _errorMsg = errorMsg(0).arg(r_code);
    if (ptype == "A")
      status = "A";	// Authorized
    else if (ptype == "V")
      status = "V";	// Voided
    else
      status = "C";	// Completed/Charged
  }
  else if (r_approved == "DECLINED")
  {
    _errorMsg = errorMsg(-92).arg(r_message);
    returnValue = -92;
    status = "D";
  }
  else if (r_approved == "ERROR")
  {
    r_error = r_message;
    _errorMsg = errorMsg(-12).arg(r_error);
    returnValue = -12;
    status = "X";
  }

  else if (r_approved.isEmpty() && ! r_message.isEmpty())
  {
    _errorMsg = errorMsg(-95).arg(r_message);
    returnValue = -95;
    status = "X";
  }

  else if (r_approved.isEmpty())
  {
    _errorMsg = errorMsg(-100).arg(r_error).arg(r_message).arg(presponse);
    returnValue = -100;
    status = "X";
  }

  _passedAvs = false; // TODO: figure out the right value to use here

  // always use the CVV checking configured on the gateway
  QString validCvv("MPU ");
  _passedCvv = validCvv.contains(r_cvv);

  if (DEBUG)
    qDebug("Paymentech:%s _passedAvs %d\t%s _passedCvv %d",
	    r_avs.toAscii().data(), _passedAvs, 
	    r_cvv.toAscii().data(), _passedCvv);

  pparams.append("ccard_id",    pccardid);
  pparams.append("currid",      pcurrid);
  pparams.append("auth_charge", ptype);
  pparams.append("type",        ptype);
  pparams.append("reforder",    (preforder.isEmpty()) ? pneworder : preforder);
  pparams.append("status",      status);
  pparams.append("avs",         r_avs);
  pparams.append("ordernum",    pneworder);
  pparams.append("xactionid",   r_ordernum);
  pparams.append("error",       r_error);
  pparams.append("approved",    r_approved);
  pparams.append("code",        r_code);
  pparams.append("shipping",    r_shipping);
  pparams.append("tax",         r_tax);
  pparams.append("ref",         r_ref);
  pparams.append("message",     r_message);

  if (ptype == "A")
    pparams.append("auth", QVariant(true, 0));
  else
    pparams.append("auth", QVariant(false, 1));

  if (DEBUG)
    qDebug("Paymentech:r_error.isEmpty() = %d", r_error.isEmpty());

  if (returnValue == 0)
    pparams.append("amount",   pamount);
  else
    pparams.append("amount",   0);	// no money changed hands this attempt

  if (DEBUG)
    qDebug("Paymentech::handleResponse returning %d %s",
           returnValue, errorMsg().toAscii().data());
  return returnValue;
}

int PaymentechProcessor::doTestConfiguration()
{
  if (DEBUG)
    qDebug("Pt:doTestConfiguration()");

  int returnValue = 0;

  if (_metricsenc->value("CCLogin").isEmpty())
  {
    _errorMsg = errorMsg(-200);
    returnValue = -200;
  }
  else if (_metricsenc->value("CCPassword").isEmpty())
  {
    _errorMsg = errorMsg(-201);
    returnValue = -201;
  }
  else if (_metricsenc->value("CCPTDivisionNumber").size() > 10)
  {
    _errorMsg = errorMsg(-202);
    returnValue = -202;
  }

  return returnValue;
}

bool PaymentechProcessor::handlesCreditCards()
{
  return true;
}