/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2010 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#include "dspSalesHistoryByCustomer.h"

#include <QVariant>
//#include <QStatusBar>
#include <QWorkspace>
#include <QMessageBox>
#include <QMenu>

#include <metasql.h>
#include "mqlutil.h"

#include <openreports.h>
#include "salesHistoryInformation.h"
#include "dspInvoiceInformation.h"

#define UNITPRICE_COL	7
#define EXTPRICE_COL	8
#define CURRENCY_COL	9
#define BUNITPRICE_COL	10
#define BEXTPRICE_COL	11
#define UNITCOST_COL	( 12 - (_privileges->check("ViewCustomerPrices") ? 0 : 5))
#define EXTCOST_COL	(13 - (_privileges->check("ViewCustomerPrices") ? 0 : 5))

/*
 *  Constructs a dspSalesHistoryByCustomer as a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'.
 *
 */
dspSalesHistoryByCustomer::dspSalesHistoryByCustomer(QWidget* parent, const char* name, Qt::WFlags fl)
    : XWidget(parent, name, fl)
{
  setupUi(this);

//  (void)statusBar();

  // signals and slots connections
  connect(_print, SIGNAL(clicked()), this, SLOT(sPrint()));
  connect(_close, SIGNAL(clicked()), this, SLOT(close()));
  connect(_query, SIGNAL(clicked()), this, SLOT(sFillList()));
  connect(_sohist, SIGNAL(populateMenu(QMenu*,QTreeWidgetItem*,int)), this, SLOT(sPopulateMenu(QMenu*)));
  connect(_showPrices, SIGNAL(toggled(bool)), this, SLOT(sHandleParams()));
  connect(_showCosts, SIGNAL(toggled(bool)), this, SLOT(sHandleParams()));

  _productCategory->setType(ParameterGroup::ProductCategory);

  _sohist->addColumn(tr("Doc. #"),              _orderColumn,    Qt::AlignLeft,   true,  "cohist_ordernumber"   );
  _sohist->addColumn(tr("Invoice #"),           _orderColumn,    Qt::AlignLeft,   true,  "invoicenumber"   );
  _sohist->addColumn(tr("Ord. Date"),           _dateColumn,     Qt::AlignCenter, true,  "cohist_orderdate" );
  _sohist->addColumn(tr("Invc. Date"),          _dateColumn,     Qt::AlignCenter, true,  "cohist_invcdate" );
  _sohist->addColumn(tr("Item Number"),         _itemColumn,     Qt::AlignLeft,   true,  "item_number"   );
  _sohist->addColumn(tr("Description"),         -1,              Qt::AlignLeft,   true,  "itemdescription"   );
  _sohist->addColumn(tr("Shipped"),             _qtyColumn,      Qt::AlignRight,  true,  "cohist_qtyshipped"  );
  if (_privileges->check("ViewCustomerPrices"))
  {
    _sohist->addColumn(tr("Unit Price"),        _priceColumn,    Qt::AlignRight,  true,  "cohist_unitprice" );
    _sohist->addColumn(tr("Ext. Price"),        _bigMoneyColumn, Qt::AlignRight,  true,  "extprice" );
    _sohist->addColumn(tr("Currency"),          _currencyColumn, Qt::AlignRight,  true,  "currAbbr" );
    _sohist->addColumn(tr("Base Unit Price"),   _bigMoneyColumn, Qt::AlignRight,  true,  "baseunitprice" );
    _sohist->addColumn(tr("Base Ext. Price"),   _bigMoneyColumn, Qt::AlignRight,  true,  "baseextprice" );
  }
  if (_privileges->check("ViewCosts"))
  {
    _sohist->addColumn( tr("Unit Cost"),  _costColumn,           Qt::AlignRight,  true,  "cohist_unitcost" );
    _sohist->addColumn( tr("Ext. Cost"),  _bigMoneyColumn,       Qt::AlignRight,  true,  "extcost" );
  }

  _showCosts->setEnabled(_privileges->check("ViewCosts"));
  _showPrices->setEnabled(_privileges->check("ViewCustomerPrices"));

  sHandleParams();

  _cust->setFocus();
}

/*
 *  Destroys the object and frees any allocated resources
 */
dspSalesHistoryByCustomer::~dspSalesHistoryByCustomer()
{
  // no need to delete child widgets, Qt does it all for us
}

/*
 *  Sets the strings of the subwidgets using the current
 *  language.
 */
void dspSalesHistoryByCustomer::languageChange()
{
  retranslateUi(this);
}

enum SetResponse dspSalesHistoryByCustomer::set(const ParameterList &pParams)
{
  XWidget::set(pParams);
  QVariant param;
  bool     valid;

  param = pParams.value("cust_id", &valid);
  if (valid)
  {
    _cust->setId(param.toInt());
    _cust->setReadOnly(TRUE);
  }

  param = pParams.value("prodcat_id", &valid);
  if (valid)
    _productCategory->setId(param.toInt());

  param = pParams.value("prodcat_pattern", &valid);
  if (valid)
    _productCategory->setPattern(param.toString());

  param = pParams.value("warehous_id", &valid);
  if (valid)
    _warehouse->setId(param.toInt());
  else
    _warehouse->setAll();

  param = pParams.value("startDate", &valid);
  if (valid)
    _dates->setStartDate(param.toDate());

  param = pParams.value("endDate", &valid);
  if (valid)
    _dates->setEndDate(param.toDate());

  if (pParams.inList("run"))
  {
    sFillList();
    return NoError_Run;
  }

  return NoError;
}

void dspSalesHistoryByCustomer::sHandleParams()
{
  if (_showPrices->isChecked())
  {
    _sohist->showColumn(UNITPRICE_COL);
    _sohist->showColumn(EXTPRICE_COL);
    _sohist->showColumn(CURRENCY_COL);
    _sohist->showColumn(BUNITPRICE_COL);
    _sohist->showColumn(BEXTPRICE_COL);
  }
  else
  {
    _sohist->hideColumn(UNITPRICE_COL);
    _sohist->hideColumn(EXTPRICE_COL);
    _sohist->hideColumn(CURRENCY_COL);
    _sohist->hideColumn(BUNITPRICE_COL);
    _sohist->hideColumn(BEXTPRICE_COL);
  }

  if (_showCosts->isChecked())
  {
    _sohist->showColumn(UNITCOST_COL);
    _sohist->showColumn(EXTCOST_COL);
  }
  else
  {
    _sohist->hideColumn(UNITCOST_COL);
    _sohist->hideColumn(EXTCOST_COL);
  }
}

void dspSalesHistoryByCustomer::sPopulateMenu(QMenu *pMenu)
{
  int menuItem;
  XTreeWidgetItem * item = (XTreeWidgetItem*)_sohist->currentItem();

  menuItem = pMenu->insertItem(tr("Edit..."), this, SLOT(sEdit()), 0);
  if (!_privileges->check("EditSalesHistory"))
    pMenu->setItemEnabled(menuItem, FALSE);

  pMenu->insertItem(tr("View..."), this, SLOT(sView()), 0);

  if (item->rawValue("invoicenumber").toString().length() > 0)
  {
    pMenu->insertSeparator();

    menuItem = pMenu->insertItem(tr("Invoice Information..."), this, SLOT(sInvoiceInformation()), 0);
  }
}

void dspSalesHistoryByCustomer::sEdit()
{
  ParameterList params;
  params.append("mode", "edit");
  params.append("sohist_id", _sohist->id());

  salesHistoryInformation newdlg(this, "", TRUE);
  newdlg.set(params);

  if (newdlg.exec() != XDialog::Rejected)
    sFillList();
}

void dspSalesHistoryByCustomer::sView()
{
  ParameterList params;
  params.append("mode", "view");
  params.append("sohist_id", _sohist->id());

  salesHistoryInformation newdlg(this, "", TRUE);
  newdlg.set(params);
  newdlg.exec();
}

void dspSalesHistoryByCustomer::sInvoiceInformation()
{
  XTreeWidgetItem * item = (XTreeWidgetItem*)_sohist->currentItem();
  if(0 == item)
    return;

  ParameterList params;
  params.append("invoiceNumber", item->rawValue("invoicenumber").toString());

  dspInvoiceInformation *newdlg = new dspInvoiceInformation();
  newdlg->set(params);
  omfgThis->handleNewWindow(newdlg);
}

void dspSalesHistoryByCustomer::sPrint()
{
  if (!_dates->allValid())
  {
    QMessageBox::warning( this, tr("Enter Valid Dates"),
                          tr("Please enter a valid Start and End Date.") );
    _dates->setFocus();
    return;
  }

  ParameterList params;
  params.append("cust_id", _cust->id());

  _productCategory->appendValue(params);
  _warehouse->appendValue(params);
  _dates->appendValue(params);
  params.append("includeFormatted");

  if (_showCosts->isChecked())
    params.append("showCosts");

  if (_showPrices->isChecked())
    params.append("showPrices");

  orReport report("SalesHistoryByCustomer", params);
  if (report.isValid())
    report.print();
  else
    report.reportError(this);
}

void dspSalesHistoryByCustomer::sFillList()
{
  if (!checkParameters())
    return;

  _sohist->clear();
  
  MetaSQLQuery mql = mqlLoad("salesHistory", "detail");
  ParameterList params;
  _dates->appendValue(params);
  _warehouse->appendValue(params);
  _productCategory->appendValue(params);
  params.append("cust_id", _cust->id());
  params.append("orderByInvcdateItem");
  q = mql.toQuery(params);
  _sohist->populate(q);
}

bool dspSalesHistoryByCustomer::checkParameters()
{
  if (isVisible())
  {
    if (!_cust->isValid())
    {
      QMessageBox::warning( this, tr("Enter Customer Number"),
                            tr("Please enter a valid Customer Number.") );
      _cust->setFocus();
      return FALSE;
    }

    if (!_dates->startDate().isValid())
    {
      QMessageBox::warning( this, tr("Enter Start Date"),
                            tr("Please enter a valid Start Date.") );
      _dates->setFocus();
      return FALSE;
    }

    if (!_dates->endDate().isValid())
    {
      QMessageBox::warning( this, tr("Enter End Date"),
                            tr("Please enter a valid End Date.") );
      _dates->setFocus();
      return FALSE;
    }
  }

  return TRUE;
}