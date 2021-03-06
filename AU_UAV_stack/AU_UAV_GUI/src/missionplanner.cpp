/***************************************************
 Coder: Jacob Dalton Conaway - jdc0019@auburn.edu
 Reviewer/Tester: Kayla Casteel - klc0025@auburn.edu
 Senior Design - Spring 2013
 Sources are in-line
 TODO: Finish Commenting and Proofread Comments
 ***************************************************/

#include "AU_UAV_GUI/missionplanner.hpp"
#include "AU_UAV_GUI/missiongenerator.hpp"
#include "AU_UAV_GUI/constants.h"
#include "../build/ui_missionplanner.h"
#include <QDebug>
#include <QWebFrame>
#include <QWebElement>
#include <QAbstractButton>
#include <QFileDialog>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QInputDialog>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTableWidget>

MissionPlanner::MissionPlanner(QWidget * parent) :
    QWidget(parent), ui(new Ui::MissionPlanner)
{
    ui->setupUi(this);
    //enable JavaScript
    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled,
            true);

    //load the HTML file
    ui->webView2->setUrl(PLANNER_MAP_URL);

    //interface to JS to prevent MissionPlanner from being accessable to the web browser
    jsInterface = new JSInterface(this);

    //connect all slots and signals
    connect(ui->doneWIthPlaneButton, SIGNAL(clicked()), this,
            SLOT(doneWithPlane()));
    connect(ui->resetMapButton, SIGNAL(clicked()), this, SLOT(resetMap()));
    connect(ui->missionButtonGroup, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(missionButtonClicked(QAbstractButton*)));
    connect(ui->fileButtonGroup, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(fileButtonClicked(QAbstractButton*)));
    connect(ui->saveFileButton, SIGNAL(clicked()), this, SLOT(saveFile()));
    connect(ui->editFileButton, SIGNAL(clicked()), this, SLOT(editFile()));
    connect(ui->setCenterButton, SIGNAL(clicked()), this,
            SLOT(setCenterFromGUI()));
    connect(ui -> planeIDSpinBox, SIGNAL(valueChanged(int)), SLOT(updatePlaneIDInMap(int)));
    connect(ui->visiblePathsList, SIGNAL(itemChanged(QListWidgetItem*)), SLOT(toggleFlightPath(QListWidgetItem*)));
    connect(ui->goToButton, SIGNAL(clicked()), this, SLOT(editPath()));
    connect(ui->generateMissionButton, SIGNAL(clicked()), this,
            SLOT(generateRandomMission()));
    connect(ui -> minValSpinBox, SIGNAL(valueChanged(int)), this, SLOT(setMaxStartVal(int)));
    connect(ui->loadFileButton, SIGNAL(clicked()), this, SLOT(loadFile()));
    connect(ui->webView2->page()->currentFrame(),
            SIGNAL(javaScriptWindowObjectCleared()), this,
            SLOT(attachObjectToJS()));
    connect(ui->showWindButton, SIGNAL(clicked()), this,
            SLOT(showWindConditions()));
    connect(ui->saveLocationButton, SIGNAL(clicked()), this,
            SLOT(saveMapLocation()));
    connect(ui->locationsComboBox, SIGNAL(currentIndexChanged(QString)), this,
            SLOT(goToSavedLocation(QString)));
    connect(jsInterface, SIGNAL(saveAltitude()), this, SLOT(saveAltitude()));
    connect(jsInterface, SIGNAL(updateFilePreview()), this,
            SLOT(updateFilePreview()));

    //set up all of the rest of our gui components.
    prepGUI();
}

MissionPlanner::~MissionPlanner()
{
    delete ui;
    //TODO:delete other variables?
}

//more initial set-up for the gui
void MissionPlanner::prepGUI()
{
    //set the default text
    ui->previewBox->setPlainText(DEFAULT_FILE_PREVIEW_TEXT);

    // restrict latitude and longitude values
    // TODO: check ranges in .ui file and make sure they are appropriate
    QDoubleValidator *latValidator = new QDoubleValidator(LAT_MIN_VAL,
            LAT_MAX_VAL, LAT_LONG_MAX_DEC_DIGITS, this);
    QDoubleValidator *longValidator = new QDoubleValidator(LONG_MIN_VAL,
            LONG_MAX_VAL, LAT_LONG_MAX_DEC_DIGITS, this);

    latValidator->setNotation(QDoubleValidator::StandardNotation);
    longValidator->setNotation(QDoubleValidator::StandardNotation);

    ui->LatitudeLineEdit->setValidator(latValidator);
    ui->LongitudeLineEdit->setValidator(longValidator);

    // http://stackoverflow.com/questions/3307758/qt-define-tab-order-programmatically
    // TODO: define tab order for all components.
    QWidget::setTabOrder(ui->LatitudeLineEdit, ui->LongitudeLineEdit);
}

//save the current field location to the locations list.
void MissionPlanner::saveMapLocation()
{
    bool ok;
    //prompt the user for the name
    QString name = QInputDialog::getText(this, ENTER_NAME_TITLE,
                                         ENTER_NAME_TEXT, QLineEdit::Normal, "", &ok);

    //if the name is valid, then save it.
    if (ok && !name.isEmpty()) {
        ui->locationsComboBox->addItem(name);
        //get the cooridinates of the map center from the JS side.
        QList < QVariant > coords =
            ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
                "getMapCenter();").toList();
        QList<double> coordsToSave;
        coordsToSave.append(coords[LAT_POS].toDouble());
        coordsToSave.append(coords[LONG_POS].toDouble());

        //save it to our locations map
        savedLocations.insert(name, coordsToSave);

        //save all of the locations to file
        saveFavorites();

        //update the combobox
        reloadComboBox();
    }

    //if the user entered an empty name, then warn them that it is not acceptable.
    if (ok && name.isEmpty()) {
        QMessageBox::warning(this, WARNING_TITLE, EMPTY_NAME_ERROR,
                             QMessageBox::Ok);
    }

    //if ok is false then the user canceled the popup, so do nothing.
    //TODO:what if they enter a name that is already used?
}

//source(line for line): http://stackoverflow.com/questions/8766633/how-to-determine-the-correct-size-of-a-qtablewidget
//used to get a size that will be large enough to show all of the info in the table
QSize MissionPlanner::myGetQTableWidgetSize(QTableWidget *t)
{
    int w = t->verticalHeader()->width() + 4; // +4 seems to be needed
    for (int i = 0; i < t->columnCount(); i++)
        w += t->columnWidth(i); // seems to include gridline (on my machine)
    int h = t->horizontalHeader()->height() + 4;
    for (int i = 0; i < t->rowCount(); i++)
        h += t->rowHeight(i);
    return QSize(w, h);
}

//show a popup with all of the locations so that the user can edit them.
void MissionPlanner::showEditLocations()
{
    locTableWidget = new QTableWidget();

    populateLocTable (locTableWidget);

    locTableWidget->setMinimumSize(myGetQTableWidgetSize(locTableWidget));

    QDialog *editLocPopup = new QDialog(this);
    editLocPopup->setAttribute(Qt::WA_DeleteOnClose);
    connect(editLocPopup, SIGNAL(rejected()), this,
            SLOT(processEditedLocations()));
    QVBoxLayout *verticalLayout = new QVBoxLayout();
    verticalLayout->addWidget(locTableWidget);
    editLocPopup->setLayout(verticalLayout);
    editLocPopup->show();
}

//fill the table with the saved location data.
void MissionPlanner::populateLocTable(QTableWidget *locTableWidget)
{
    //clear our helper map and the table.
    tableIndexToOrigName.clear();
    locTableWidget->clearContents();

    //we must set the row and column count for the table to display correctly.
    locTableWidget->setRowCount(savedLocations.count());
    locTableWidget->setColumnCount(LOC_TABLE_NUM_OF_COLS);

    //add our headers and set the table to autosize column widths.
    locTableWidget->setHorizontalHeaderLabels(LOC_TABLE_HEADER);
    locTableWidget->horizontalHeader()->setResizeMode(
        QHeaderView::ResizeToContents);
    //since we use rows as keys in our table, we do not want the order to be changed.
    locTableWidget->setSortingEnabled(false);

    int row = 0;

    //for each location
    foreach(QString name, savedLocations.keys()) {
        //we need to save the original name of the location, in case the user decides to edit it,
        //because it is the primary key for the saved locations map. The easiest way to save the
        //original name is to simply insert it into a new map indexed by the row number. This works
        //because the table is rebuilt everytime the edit button is clicked and sorting is disabled.
        tableIndexToOrigName.insert(row, name);

        //set up the name entry box
        QLineEdit *nameLineEdit = new QLineEdit(name, locTableWidget);
        nameLineEdit->setFrame(false);
        locTableWidget->setCellWidget(row, NAME_COLUMN, nameLineEdit);

        //get our lat and long
        QList<double> coordinate = savedLocations.value(name);
        QString latVal = QString();
        latVal.setNum(coordinate[LAT_POS], FLOAT_FORMATTER);
        QString longVal = QString();
        longVal.setNum(coordinate[LONG_POS], FLOAT_FORMATTER);

        //set up the lat long edit boxes and restrict their input to valid coordinates only
        QLineEdit *latLineEdit = new QLineEdit(latVal, locTableWidget);
        QLineEdit *longLineEdit =new QLineEdit(longVal, locTableWidget);

        QDoubleValidator *latValidator = new QDoubleValidator(LAT_MIN_VAL,
                LAT_MAX_VAL, LAT_LONG_MAX_DEC_DIGITS, this);
        QDoubleValidator *longValidator = new QDoubleValidator(LONG_MIN_VAL,
                LONG_MAX_VAL, LAT_LONG_MAX_DEC_DIGITS, this);

        latValidator->setNotation(QDoubleValidator::StandardNotation);
        longValidator->setNotation(QDoubleValidator::StandardNotation);

        latLineEdit->setValidator(latValidator);
        longLineEdit->setValidator(longValidator);

        latLineEdit->setFrame(false);
        longLineEdit->setFrame(false);

        //add the lat/long edit boxes and the delete item checkbox.
        locTableWidget->setCellWidget(row, LAT_COLUMN, latLineEdit);
        locTableWidget->setCellWidget(row, LONG_COLUMN, longLineEdit);
        QTableWidgetItem *deleteCheckBox = new QTableWidgetItem();
        deleteCheckBox->setCheckState(Qt::Unchecked);
        locTableWidget->setItem(row, DELETE_COLUMN, deleteCheckBox);
        row++;
    }
}

//process the table after the user has finished editing
void MissionPlanner::processEditedLocations()
{
//if the table is null, then return
    if (!locTableWidget) {
        return;
    }

//for each location
    for (int row = 0; row < locTableWidget->rowCount(); row++) {
        //extract the info from the table. The lat and long are protected by validators.
        QString name =
            ((QLineEdit*) locTableWidget->cellWidget(row, NAME_COLUMN))->text();
        double latVal =
            ((QLineEdit*) locTableWidget->cellWidget(row, LAT_COLUMN))->text().toDouble();
        double longVal =
            ((QLineEdit*) locTableWidget->cellWidget(row, LONG_COLUMN))->text().toDouble();
        QList<double> coord = QList<double>() << latVal << longVal;
        int shouldDelete = locTableWidget->item(row, DELETE_COLUMN)->checkState();

        //if the user has not changed the location name. (i.e. the original name is the same as the current name)
        if (name.compare(tableIndexToOrigName.value(row), Qt::CaseSensitive) == 0) {
            //delete it if we need to. else, just update the coords (I felt it was easier to just update them,
            //rather than add a new map of original coords and making comparisons.)
            if (shouldDelete == Qt::Checked) {
                savedLocations.remove(name);
            } else {
                savedLocations[name] = coord;
            }
            //the user has edited the name.
        } else {
            //we need to remove the old one regardless
            savedLocations.remove(tableIndexToOrigName.value(row));

            //if it is not marked for deletion then add the edited location back
            if (shouldDelete != Qt::Checked) {
                savedLocations.insert(name, coord);
            }
        }
    }

//save the favorites map to file
    saveFavorites();
//update the combobox
    reloadComboBox();
}

//update the combo box
void MissionPlanner::reloadComboBox()
{
//completely clear the box.
    ui->locationsComboBox->clear();

//add the title item
    ui->locationsComboBox->addItem(LOC_COMBO_BOX_TITLE);

//if we don't have any saved locations then disable the combo box and return to prevent unnecessary computation.
    if (savedLocations.isEmpty()) {
        ui->locationsComboBox->setEnabled(false);
        return;
    }

//add our edit locations "button" (i.e. item) and our separator lines
    ui->locationsComboBox->setEnabled(true);
    ui->locationsComboBox->addItem(EDIT_LOC_TITLE);
    ui->locationsComboBox->insertSeparator(SEP_LINE_INDEX_1);
    ui->locationsComboBox->insertSeparator(SEP_LINE_INDEX_2);

// prevent "saved locations" title from being selectable
// this is the easiest way I have found to do it.
    QStandardItemModel * model = qobject_cast<QStandardItemModel *>(
                                     ui->locationsComboBox->model());
    QModelIndex firstIndex = model->index(0, ui->locationsComboBox->modelColumn(),
                                          ui->locationsComboBox->rootModelIndex());
    QStandardItem * firstItem = model->itemFromIndex(firstIndex);

    firstItem->setEnabled(false);

//actually add the items from the saved locations map
    foreach(QString name, savedLocations.keys()) {
        ui->locationsComboBox->addItem(name);
    }
}

//the listener for the combobox
void MissionPlanner::goToSavedLocation(QString index)
{
//if the user "pressed" the edit locations "button" then show the editable table
    if (index.compare(EDIT_LOC_TITLE, Qt::CaseSensitive) == 0) {
        showEditLocations();
    }

    //if the location does exist (which it always should) then set the map to it.
    else if (savedLocations.contains(index)) {
        QList<double> coords = savedLocations.value(index);
        double lat = coords[LAT_POS];
        double longitude = coords[LONG_POS];
        QString centerCommand = QString("setCenter(%1, %2);").arg(lat).arg(longitude);
        ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
            centerCommand);
    } else if ((index.isEmpty())
               || (index.compare(LOC_COMBO_BOX_TITLE, Qt::CaseSensitive) == 0)) {
//do nothing
    }
    //this should never happen.
    else {
        QMessageBox::warning(this, WARNING_TITLE, MISSING_LOC_ERROR, QMessageBox::Ok);
    }

    //http://stackoverflow.com/questions/4340415/qcombobox-set-selected-item
    //we probably could just go with 0 for the index, but I wanted to make sure that we had the right one,
//and it will not add that much more computation.
    int loopIndex = ui->locationsComboBox->findText(LOC_COMBO_BOX_TITLE);
    if (loopIndex != NOT_FOUND) { // -1 for not found
        ui->locationsComboBox->setCurrentIndex(loopIndex);
    }
}

//expose our interface class to JavaScript
void MissionPlanner::attachObjectToJS()
{
    ui->webView2->page()->currentFrame()->addToJavaScriptWindowObject(
        QString("jsInterface"), jsInterface);
}

//generate a random mission from the chosen parameters
//and display it on the map and preview file
void MissionPlanner::generateRandomMission()
{
//This method requires the map to be reset which results in a
//warning screen. If the user chooses cancel (a false return) we are done.
    if (!resetMap()) {
        return;
    }

//set up our square on the map and get the NorthWest corner of it.
    int sideLength = ui->fieldSizeSpinBox->value();
//the JS method returns the NW corner. We just have to extract it.
    QString command = QString("addRandomBounds(%1);").arg(sideLength);
    QVariant NWVarient =
        ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
            command);
    QList < QVariant > NWCoordList = NWVarient.toList();

//set up all of the needed variables for the mission generator.
    double northMostLat = NWCoordList[LAT_POS].toDouble();
    double westMostLong = NWCoordList[LONG_POS].toDouble();
    int numOfPlanes = ui->numOfPlanesSpinBox->value();
    int minAltitude = ui->minValSpinBox->value();
    int maxAltitude = ui->maxValSpinBox->value();
    int numOfWayPoints = ui->numOfWPSpinBox->value();

//prepare our random header file
    randSettingsHeader = RAND_FILE_HEADER.arg(numOfPlanes).arg(sideLength).arg(
                             numOfWayPoints).arg(northMostLat).arg(westMostLong).arg(minAltitude).arg(
                             maxAltitude);

//create a new mission using the mission generator
//A static method would probably be better, but it would need too many parameters.
//Also, the class approach increases its reusability.
    MissionGenerator * newMissionGen = new MissionGenerator(westMostLong,
            northMostLat, minAltitude, maxAltitude, numOfWayPoints);

//get the mission data
    QList < QList<QVariant> > planeData = newMissionGen->generateCourse(numOfPlanes,
                                          sideLength);

//populate the map from our mission data
    populateMapFromQList (planeData);
}

//populate the map from our mission data
void MissionPlanner::populateMapFromQList(QList<QList<QVariant> > planeData)
{
//reset the map, but do not reset the rectangle.
//Also, temporarily disable all callback listeners to ensure a quick response.
    QString resetCommand = QString("resetMap(false);clearFlightPathListeners();");
    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        resetCommand);

//manually add the data coordinate by coordinate, and save the altitude data for later
    for (int pIndex = 0; pIndex < planeData.count(); pIndex++) {
        int planeID = planeData[pIndex][ID_LIST_POS].toInt();
        double latitude = planeData[pIndex][LAT_LIST_POS].toDouble();
        double longitude = planeData[pIndex][LONG_LIST_POS].toDouble();
        altitudes.insert(planeID, planeData[pIndex][ALT_LIST_POS].toDouble());
        QString addPointcommand =
            QString("manuallyAddPoint(%1, %2, %3);").arg(planeID).arg(latitude).arg(
                longitude);
        ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
            addPointcommand);
    }

    //set all of the paths to non-editable and re-enable the callback listeners.
    QString noEditCommand = QString(
                                "reinstateFlightPathListeners();setEditableForAllPaths(false);");
    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        noEditCommand);

    //update the file preview with the new data
    updateFilePreview();
}

//This method works by programmatically changing the plane ID spinbox
void MissionPlanner::editPath()
{
    //see which plane the user has selected
    if (!ui->visiblePathsList->selectedItems().isEmpty()) {
        int firstSelected =
            ui->visiblePathsList->selectedItems().first()->text().remove(
                PLANE_NUM_PREFIX).toInt();

//check to see if the spinbox is already on the right plane
        if (ui->planeIDSpinBox->value() == firstSelected) {
            //if it is the same as the current value then the slot will never get called, so we have to call it manually.
            updatePlaneIDInMap(firstSelected);
        } else {
            //if it is not, then we can simply set it
            ui->planeIDSpinBox->setValue(firstSelected);
        }

//set the altitude to the appropriate value
        if (firstSelected < altitudes.size()) {
            ui->altitudeSpinBox->setValue(altitudes[firstSelected]);
        }
    }
}

//set the center of the map to the coordinates given by the user
void MissionPlanner::setCenterFromGUI()
{
    // all other cases are protected by validator
    if (ui->LatitudeLineEdit->text().isEmpty()
            || ui->LatitudeLineEdit->text().isNull()
            || ui->longitudeLabel->text().isEmpty()
            || ui->longitudeLabel->text().isNull()) {
        return;
    }

    double lat = ui->LatitudeLineEdit->text().toDouble();
    double longitude = ui->LongitudeLineEdit->text().toDouble();
    QString centerCommand = QString("setCenter(%1, %2);").arg(lat).arg(longitude);

    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        centerCommand);
}

void MissionPlanner::doneWithPlane()
{
    QString clearCommand = "clearAllMarkers();setEditableForAllPaths(false);";
    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        clearCommand);

    //increment the plane ID spin box and update the file preview
    ui->planeIDSpinBox->setValue(ui->planeIDSpinBox->value() + 1);
    updateFilePreview();
}

//update the file preview text box
void MissionPlanner::updateFilePreview()
{
    //clear the text box helper variables
    firstPlaneCoords.clear();
    bodyText.clear();

    //get coordinates associated with current flight paths
    QString getCoordsCommand = "getAllPlaneCoordinates();";
    QVariant coords =
        ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
            getCoordsCommand);
    QList < QVariant > planesList = coords.toList();

    //for each plane
    for (int pIndex = 0; pIndex < planesList.count(); pIndex++) {
//add the comment line
        if (planesList[pIndex].toList().count() > 0) {
            bodyText += PLANE_HEADER_LINE.arg(pIndex).arg(
                            planesList[pIndex].toList().count());

            //add to the list of planes
            addToPlanesList(pIndex);
        }

//create a list of the waypoints assocaited with the current plane
        QList < QVariant > waypointList = planesList[pIndex].toList();

//for each waypoint
        for (int index = 0; index < waypointList.count(); index++) {
            //extract the lat long coordinate
            QList < QVariant > coordinate = waypointList[index].toList();

            //get the altitude from the altitudes map
            QString altitude;
            //if altitude is not found, then assign it the default altitude value.
            altitude.setNum(
                altitudes.value(coordinate[ID_LIST_POS].toInt(), DEFAULT_ALTITUDE));

            QString coordinateString;

            //create the coordinate string
            if (ui->courseFileButton->isChecked()) {
                coordinateString = PLANE_COORD_LINE_COURSE.arg(
                                       coordinate[ID_LIST_POS].toInt()).arg(
                                       coordinate[LAT_LIST_POS].toDouble(), LAT_LONG_FIELD_WIDTH,
                                       FLOAT_FORMATTER, LAT_LONG_NUM_OF_DIGITS_FILE).arg(
                                       coordinate[LONG_LIST_POS].toDouble(), LAT_LONG_FIELD_WIDTH,
                                       FLOAT_FORMATTER, LAT_LONG_NUM_OF_DIGITS_FILE).arg(
                                       altitude.toDouble(), LAT_LONG_FIELD_WIDTH, FLOAT_FORMATTER,
                                       LAT_LONG_NUM_OF_DIGITS_FILE);

            } else {
                coordinateString = PLANE_COORD_LINE_PATH.arg(
                                       coordinate[LAT_LIST_POS].toDouble(), LAT_LONG_FIELD_WIDTH,
                                       FLOAT_FORMATTER, LAT_LONG_NUM_OF_DIGITS_FILE).arg(
                                       coordinate[LONG_LIST_POS].toDouble(), LAT_LONG_FIELD_WIDTH,
                                       FLOAT_FORMATTER, LAT_LONG_NUM_OF_DIGITS_FILE).arg(
                                       altitude.toInt());
            }

            //if it is the first coordinate for the plane it goes on top
            if (index == 0) {
                firstPlaneCoords.append(coordinateString);
            } else {
                bodyText += coordinateString;
            }
        } //done with waypoint
    } //done with plane

//make sure no planes were deleted
    updatePlanesList();

//actually update the text box
    updateTextField();
}

//remove planes from the list that no longer have coordinates
void MissionPlanner::updatePlanesList()
{
    QString getActiveCommand = "getActivePlanes();";
    QVariant activePlanes =
        ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
            getActiveCommand);
    QList < QVariant > activePlanesList = activePlanes.toList();

//if the number of active planes does not equal the number of
//planes in the list, then we need to prune the list
    if (activePlanesList.size() != ui->visiblePathsList->count()) {
//for each plane in the list
        for (int row = 0; row < ui->visiblePathsList->count(); row++) {
            QListWidgetItem *item = ui->visiblePathsList->item(row);
            //the list is of QVariant doubles so we need a QVariant double
            QVariant planeID = item->text().remove(PLANE_NUM_PREFIX).toDouble();
            //check to see if it is active. If not, delete it.
            if (!activePlanesList.contains(planeID)) {
                //deleting the item also removes it from the list
                delete item;
            }
        }
    }
}

//add a plane to the list of planes
void MissionPlanner::addToPlanesList(int idToAdd)
{
//check to see if it is already in the list
    QString label = QString(PLANE_NUM_PREFIX + "%1").arg(idToAdd);
    QList<QListWidgetItem *> results = ui->visiblePathsList->findItems(label,
                                       Qt::MatchExactly);
//if it is not then add it
    if (results.size() <= 0) {

//set up the color-coded icon
        QPixmap pixmap(ICON_PIXMAP_SIZE, ICON_PIXMAP_SIZE);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(Qt::black, ICON_CIRCLE_LINE_WIDTH);
        painter.setPen(pen);
        QBrush brush(QColor (COLORS[idToAdd]));
        painter.setBrush(brush);
        painter.drawEllipse(CIRCLE_X_COORD, CIRCLE_Y_COORD, CIRCLE_WIDTH,
                            CIRCLE_HEIGHT);

        QIcon itemIcon(pixmap);
        QListWidgetItem * item = new QListWidgetItem(itemIcon, label,
                ui->visiblePathsList);

        item->setCheckState(Qt::Checked);
        ui->visiblePathsList->addItem(item);
    }
}

void MissionPlanner::updateTextField()
{
    QString headerText;

//set the header to either the manual header or
//random file header.
    if (ui->randomMissionButton->isChecked()) {
        headerText = randSettingsHeader;
    } else {
        headerText = MAN_FILE_HEADER;
//if it is a path file, change the wording
        if (ui->pathFileButton->isChecked()) {
            headerText.replace(COURSE_TAG, PATH_TAG);
            headerText.replace(PLANE_ID_TAG, QString(FILE_COMMENT_TAG));
        }
    }

//add each starting waypoint to the header
    foreach(QString firstCoord, firstPlaneCoords) {
        headerText += firstCoord;
    }

//if the file is a path file, then remove the plane id tag
    if (ui->pathFileButton->isChecked()) {
        bodyText.replace(PLANE_ID_TAG, QString(FILE_COMMENT_TAG));
    }

//combine the header and body
    ui->previewBox->setPlainText(headerText + bodyText);
}

void MissionPlanner::updatePlaneIDInMap(int newID)
{
    QString updateIDCommand = QString("setCurrentID(%1);").arg(newID);
    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        updateIDCommand);

    autoEnablePathVisibility(newID);
}

//if the path has been hidden, then show it
void MissionPlanner::autoEnablePathVisibility(int planeID)
{
    QString stringID = QString(PLANE_NUM_PREFIX + "%1").arg(planeID);
    QList<QListWidgetItem *> list = ui->visiblePathsList->findItems(stringID,
                                    Qt::MatchExactly);

//make sure the item exists
    if (!list.isEmpty()) {
        QListWidgetItem * item = list.first();

//check the item - item changed slot will be called
        item->setCheckState(Qt::Checked);
    }
}

//reset all of the data associated with the file/text box
void MissionPlanner::resetFileData()
{
//TODO: should I also clear randSettings header and loaded file header?
    bodyText.clear();
    firstPlaneCoords.clear();
}

//show or hide the flight path of the plane represented by item.
void MissionPlanner::toggleFlightPath(QListWidgetItem * item)
{
//get the planeID
    int planeID = item->text().remove(PLANE_NUM_PREFIX).toInt();
    QString visible = "false";

//if the item is checked
    if (item->checkState() > 0) {
        visible = "true";
    }

//send it to JS
    QString toggleVisCommand =
        QString("togglePathVisibility(%1, %2)").arg(planeID).arg(visible);
    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        toggleVisCommand);
}

//allow the user to edit the file preview text box
void MissionPlanner::editFile()
{
    if (ui->editFileButton->text().compare(EDIT_FILE_TITLE, Qt::CaseSensitive)
            == 0) {
        ui->previewBox->setReadOnly(false);
        ui->editFileButton->setText(DONE_EDITING_TITLE);
    } else if (ui->editFileButton->text().compare(DONE_EDITING_TITLE,
               Qt::CaseSensitive) == 0) {
        ui->previewBox->setReadOnly(true);
        ui->editFileButton->setText(EDIT_FILE_TITLE);

//resetMap with no cancel option, no warning message, and without the reset preview box option
        if (!resetMap(true, true, false)) {
            return;
        }

//get the edited text from the preview box.
        QString editedFile = ui->previewBox->toPlainText();

//parse and load the course file
        QTextStream inStream(&editedFile);
        QString fullFile = parseAndLoadCourseFile(inStream);

//add a disclaimer that the file has been edited.
        if (!fullFile.contains(EDIT_DISCLAIMER)) {
            fullFile.prepend(EDIT_DISCLAIMER);
        }

//set the preview to the reconstructed file
        ui->previewBox->setPlainText(fullFile);
    }
}

//call the JS function for wind conditions
void MissionPlanner::showWindConditions()
{
    QString function = QString("showWindConditions()");

    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        function);
}

//save the data in the text box as our file
void MissionPlanner::saveFile()
{
// http://www.qtcentre.org/threads/29622-writing-to-a-text-file

    /* Try and open a file for output */

// http://qt-project.org/doc/qt-4.8/qfiledialog.html#getOpenFileName
    QString outputFilename;

    if (ui->courseFileButton->isChecked()) {
        outputFilename = QFileDialog::getSaveFileName(this, SAVE_FILE_TITLE,
                         packagePath + COURSE_DIR_POSTFIX, COURSE_FILE_TYPE);

//add the extension if the user forgot it.
        if (!(outputFilename.endsWith(COURSE_FILE_EXT))) {
            outputFilename.append(COURSE_FILE_EXT);
        }

    } else if (ui->pathFileButton->isChecked()) {
        outputFilename = QFileDialog::getSaveFileName(this, SAVE_FILE_TITLE,
                         packagePath + PATH_DIR_POSTFIX, PATH_FILE_TYPE);

//add the extension if the user forgot it.
        if (!(outputFilename.endsWith(PATH_FILE_EXT))) {
            outputFilename.append(PATH_FILE_EXT);
        }
    }

//set up and open the output file
    QFile outputFile(outputFilename);
    outputFile.open(QIODevice::WriteOnly);

    /* Check it opened OK */
    if (!outputFile.isOpen()) {
        QMessageBox::warning(this, WARNING_TITLE, SAVE_FILE_WARNING, QMessageBox::Ok);
        return;
    }

    /* Point a QTextStream object at the file */
    QTextStream outStream(&outputFile);

    /* Write the info to the file */
    outStream << ui->previewBox->toPlainText();

    /* Close the file */
    outputFile.close();
}

void MissionPlanner::missionButtonClicked(QAbstractButton * button)
{
//No Cancel Option
    if (!resetMap(true)) {
        return;
    }

//call the appropriate methods based on the mission selected.
    if (button == ui->manualMissionButton) {
        toggleManualWidgets(true);
        toggleRandomWidgets(false);
    } else {
        toggleManualWidgets(false);
        toggleRandomWidgets(true);
    }
}

void MissionPlanner::fileButtonClicked(QAbstractButton * button)
{
//No Cancel Option
    if (!resetMap(true)) {
        return;
    }

//disable and re-enable all of the appropriate buttons and labels.
    if (button == ui->pathFileButton) {
        ui->doneWIthPlaneButton->setEnabled(false);
        ui->planeIDSpinBox->setValue(MIN_PLANE_ID);
        ui->planeIDSpinBox->setEnabled(false);
        ui->planeIDLabel->setEnabled(false);
        ui->numOfPlanesSpinBox->setEnabled(false);
        ui->numOfPlanesLabel->setEnabled(false);
    } else {
        if (ui->randomMissionButton->isChecked()) {
            ui->numOfPlanesLabel->setEnabled(true);
            ui->numOfPlanesSpinBox->setEnabled(true);
        }

        if (ui->manualMissionButton->isChecked()) {
            ui->doneWIthPlaneButton->setEnabled(true);
            ui->planeIDSpinBox->setEnabled(true);
            ui->planeIDLabel->setEnabled(true);
        }
    }
}

void MissionPlanner::toggleManualWidgets(bool enabled)
{
    ui->manualMissionLabel->setEnabled(enabled);
    ui->planeIDLabel->setEnabled(enabled);
    ui->planeIDSpinBox->setEnabled(enabled);
    ui->altitudeLabel->setEnabled(enabled);
    ui->altitudeSpinBox->setEnabled(enabled);

    if (enabled && (!ui->pathFileButton->isChecked())) {
        ui->doneWIthPlaneButton->setEnabled(enabled);
    } else {
        ui->doneWIthPlaneButton->setEnabled(false);
    }
}

void MissionPlanner::toggleRandomWidgets(bool enabled)
{
    ui->randomMissionLabel->setEnabled(enabled);
    ui->numOfWPLabel->setEnabled(enabled);
    ui->numOfWPSpinBox->setEnabled(enabled);

// If we are renabling and the pathFile is not selected
    if (enabled) {
        ui->pathFileButton->setChecked(false);
        ui->pathFileButton->setEnabled(false);
        ui->courseFileButton->setChecked(true);
    } else {
        ui->pathFileButton->setEnabled(true);
    }

    ui->numOfPlanesLabel->setEnabled(enabled);
    ui->numOfPlanesSpinBox->setEnabled(enabled);
    ui->fieldSizeLabel->setEnabled(enabled);
    ui->fieldSizeSpinBox->setEnabled(enabled);
    ui->minValLabel->setEnabled(enabled);
    ui->minValSpinBox->setEnabled(enabled);
    ui->maxValLabel->setEnabled(enabled);
    ui->maxValSpinBox->setEnabled(enabled);
    ui->generateMissionButton->setEnabled(enabled);
}

//reset everything
bool MissionPlanner::resetMap(bool noCancel, bool silent,
                              bool resetPreviewBox)
{
    if (silent) {
//do nothing
    }
//check to see if a warning is needed
    else if (ui->previewBox->toPlainText().compare(DEFAULT_FILE_PREVIEW_TEXT)
             != 0) {

//show a warning message
        QMessageBox *box;
//http://stackoverflow.com/questions/9264357/qmessagebox-addbutton-using-standard-icon-display
        if (noCancel) {
            box = new QMessageBox(WARNING_TITLE, RESET_DATA_WARNING,
                                  QMessageBox::NoIcon, QMessageBox::NoButton, QMessageBox::Yes,
                                  QMessageBox::No);
        } else {
            box = new QMessageBox(WARNING_TITLE, RESET_DATA_WARNING,
                                  QMessageBox::NoIcon, QMessageBox::Cancel, QMessageBox::Yes,
                                  QMessageBox::No);
        }

        int result = box->exec();

        if (result == QMessageBox::Cancel) {
            return false;
        } else if (result == QMessageBox::Yes) {
            saveFile();
        }

    }
    QString resetMapCommand = "resetMap();";

    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        resetMapCommand);
    ui->visiblePathsList->clear();
    resetFileData();
    if (resetPreviewBox) {
        ui->previewBox->setPlainText(DEFAULT_FILE_PREVIEW_TEXT);
    }
    altitudes.clear();
    return true;
}

//always ensure the max val's minimum is >= the min val
void MissionPlanner::setMaxStartVal(int value)
{
    ui->maxValSpinBox->setMinimum(value);
}

//record selected altitude
void MissionPlanner::saveAltitude()
{
    altitudes.insert(ui->planeIDSpinBox->value(), ui->altitudeSpinBox->value());
}

void MissionPlanner::loadFile()
{
    if (!resetMap()) {
        return;
    }

    /* Try and open a file for output */
// http://qt-project.org/doc/qt-4.8/qfiledialog.html#getOpenFileName
    QString inputFilename;

    if (ui->courseFileButton->isChecked()) {
        inputFilename = QFileDialog::getOpenFileName(this, OPEN_FILE_TITLE,
                        packagePath + COURSE_DIR_POSTFIX, COURSE_FILE_TYPE);
    } else if (ui->pathFileButton->isChecked()) {
        inputFilename = QFileDialog::getOpenFileName(this, OPEN_FILE_TITLE,
                        packagePath + PATH_DIR_POSTFIX, PATH_FILE_TYPE);
    }

    if (inputFilename.isNull()) {
        return;
    }

    QFile outputFile(inputFilename);

    outputFile.open(QIODevice::ReadOnly);

    /* Check it opened OK */
    if (!outputFile.isOpen()) {
        QMessageBox::warning(this, WARNING_TITLE, SAVE_FILE_WARNING, QMessageBox::Ok);
    }

    QTextStream inStream(&outputFile);
    QString fullFile = parseAndLoadCourseFile(inStream);

    outputFile.close();

//set the preview to the reconstructed file
    ui->previewBox->setPlainText(fullFile);
}

QString MissionPlanner::parseAndLoadCourseFile(QTextStream& inStream)
{
    QString fullFile;
    QList < QList<QVariant> > allCoords;
    bool isRandomFile = false;

//read line by line
    while (!inStream.atEnd()) {
        QString line = inStream.readLine();

//reconstruct the file as we go
        fullFile.append(line);
        fullFile.append("\n");

//if line is a comment skip it
        if (line.startsWith(FILE_COMMENT_TAG) || line.isEmpty() || line.isNull()) {
            //if we see the random tag then mark this file as a random file
            if (line.startsWith(RANDOM_FILE_TAG)) {
                isRandomFile = true;
            }
            continue;
        }

// source for skipEmptyParts: https://bugreports.qt-project.org/browse/QTBUG-9043
        QStringList data = line.split(FILE_LINE_DELIM, QString::SkipEmptyParts);

//if the number of items in the coordinate string does not match the expected value then display an error.
        if ((ui->courseFileButton->isChecked() && data.size() < ITEMS_PER_LINE)
                || (ui->pathFileButton->isChecked() && data.size() < ITEMS_PER_LINE - 1)) {
            QMessageBox::warning(this, WARNING_TITLE, FILE_PARSE_ERROR,
                                 QMessageBox::Ok);
            return ui->previewBox->toPlainText();
        }

//set up a QList for this individual line/plane
        QList < QVariant > planeCoords;

        bool idOK;
        bool latOK;
        bool longOK;
        bool altOK;
        int planeID;
        double latitude;
        double longitude;
        double altitude;

//check to see if the file is a course file or a path file. if it is a path file then we need to set the plane id to 0.
        if (ui->courseFileButton->isChecked()) {
            planeID = data[ID_LIST_POS].toInt(&idOK);
            latitude = data[LAT_LIST_POS].toDouble(&latOK);
            longitude = data[LONG_LIST_POS].toDouble(&longOK);
            altitude = data[ALT_LIST_POS].toDouble(&altOK);
        } else {
            planeID = 0;
            idOK = true;
            latitude = data[LAT_LIST_POS - 1].toDouble(&latOK);
            longitude = data[LONG_LIST_POS - 1].toDouble(&longOK);
            altitude = data[ALT_LIST_POS - 1].toDouble(&altOK);
        }

//if any of the data items cannot be parsed, then display an error message.
        if ((!idOK) || (!latOK) || (!longOK) || (!altOK)) {
            QMessageBox::warning(this, WARNING_TITLE, FILE_PARSE_ERROR,
                                 QMessageBox::Ok);
            return ui->previewBox->toPlainText();
        }

//if any of the values are out of range, then display an error message.
        if ((planeID < MIN_PLANE_ID) || (planeID > MAX_PLANE_ID)
                || (latitude < LAT_MIN_VAL) || (latitude > LAT_MAX_VAL)
                || (longitude < LONG_MIN_VAL) || (longitude > LONG_MAX_VAL)
                || (altitude < MIN_ALTITUDE)) {
            QMessageBox::warning(this, WARNING_TITLE, FILE_PARSE_ERROR,
                                 QMessageBox::Ok);
            return ui->previewBox->toPlainText();
        }

        planeCoords.append(QVariant(planeID));
        planeCoords.append(QVariant(latitude));
        planeCoords.append(QVariant(longitude));
        planeCoords.append(QVariant(altitude));

//save it to the list of all plane coordinates
        allCoords.append(planeCoords);
    }

//populate the map from the file data.
    populateMapFromQList (allCoords);

    QString endOfLoadCommands;
    if (isRandomFile) {
        endOfLoadCommands = "createRectangleAfterLoad();";
    } else {
        endOfLoadCommands = "fitToBounds();";
    }

    endOfLoadCommands.append("clearAllMarkers();setEditableForAllPaths(false);");
    ui->webView2->page()->currentFrame()->documentElement().evaluateJavaScript(
        endOfLoadCommands);

    return fullFile;
}

void MissionPlanner::setPackagePathAndLoadFavLocs(QString newPath)
{
    packagePath = newPath;
    
    //load the saved favorite locations for the planner map.
    //This is dependent on the package path which is set asynchronously from 
    //MainWindow, which is why the method call is here. Previosly, the favorites file path
    //was not dependent on the package path.
    loadFavorites();
}

void MissionPlanner::saveFavorites()
{
    QFile file(packagePath + SAVED_LOC_PATH_POSTFIX);
    file.open(QIODevice::WriteOnly);

//if the file cannot be opened to be saved, then display an error message.
    if (!file.isOpen()) {
        QMessageBox::warning(this, WARNING_TITLE, LOC_SAVE_ERROR, QMessageBox::Ok);
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_4_6);
    out << savedLocations;
    file.close();
}

void MissionPlanner::loadFavorites()
{
    QFile file(packagePath + SAVED_LOC_PATH_POSTFIX);
    file.open(QIODevice::ReadOnly);

//if the file cannot be opened, then disable the combo box.
    if (!file.isOpen()) {
        ui->locationsComboBox->setEnabled(false);
        return;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_4_6);
    in >> savedLocations;
    file.close();

    reloadComboBox();
}
