import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtGraphicalEffects 1.0

import AtomicDEX.MarketMode 1.0
import AtomicDEX.TradingError 1.0

import "../../../Components"
import "../../../Constants"

FloatingBackground {
    id: root
    radius: 0
    show_shadow: false
    function focusVolumeField() {
        input_volume.field.forceActiveFocus()
    }

    readonly property string total_amount: API.app.trading_pg.total_amount

    readonly property bool can_submit_trade:  last_trading_error === TradingError.None

    // Will move to backend: Minimum Fee
    function getMaxBalance() {
        if(General.isFilled(base_ticker))
            return API.app.get_balance(base_ticker)

        return "0"
    }

    // Will move to backend: Minimum Fee
    function getMaxVolume() {
        // base in this orderbook is always the left side, so when it's buy, we want the right side balance (rel in the backend)
        const value = sell_mode ? API.app.trading_pg.orderbook.base_max_taker_vol.decimal :
                                  API.app.trading_pg.orderbook.rel_max_taker_vol.decimal

        if(General.isFilled(value))
            return value

        return getMaxBalance()
    }
    function setMinimumAmount(value){
        API.app.trading_pg.min_trade_vol = value
    }

    function reset() {
    }

    //implicitHeight: form_layout.height

    ColumnLayout {
        id: form_layout
        width: parent.width

        ColumnLayout {
            Layout.alignment: Qt.AlignTop

            Layout.fillWidth: true
            spacing: 15
            Item {
                Layout.fillWidth: true
                Layout.bottomMargin: input_volume.field.font.pixelSize
                height: input_volume.height

                AmountFieldWithInfo {
                    id: input_price

                    width: parent.width

                    field.left_text: qsTr("Price")
                    field.right_text: right_ticker
                    enabled: !(API.app.trading_pg.preffered_order.price!==undefined)
                    field.text: backend_price
                    field.onTextChanged: setPrice(field.text)
                }

                DefaultText {
                    id: price_usd_value
                    anchors.right: input_price.right
                    anchors.top: input_price.bottom
                    anchors.topMargin: 7

                    text_value: General.getFiatText(non_null_price, right_ticker)
                    font.pixelSize: input_price.field.font.pixelSize

                    CexInfoTrigger {}
                }
            }


            Item {
                Layout.fillWidth: true
                Layout.bottomMargin: input_volume.field.font.pixelSize
                height: input_volume.height

                AmountFieldWithInfo {
                    id: input_volume
                    width: parent.width
                    enabled: !multi_order_enabled

                    field.left_text: qsTr("Volume")
                    field.right_text: left_ticker
                    field.placeholderText: sell_mode ? qsTr("Amount to sell") : qsTr("Amount to receive")

                    field.text: backend_volume
                    field.onTextChanged: setVolume(field.text)
                }

                DefaultText {
                    anchors.right: input_volume.right
                    anchors.top: input_volume.bottom
                    anchors.topMargin: price_usd_value.anchors.topMargin

                    text_value: General.getFiatText(non_null_volume, left_ticker)
                    font.pixelSize: input_volume.field.font.pixelSize

                    CexInfoTrigger {}
                }
            }

            DefaultRangeSlider {
                function getRealValue() {
                    return first.position * (first.to - first.from)
                }
                function getRealValue2() {
                    return second.position * (second.to - second.from)
                }

                enabled: input_volume.field.enabled && !(!sell_mode && General.isZero(non_null_price)) && to > 0
                Layout.fillWidth: true
                from: 0
                to: Math.max(0, parseFloat(max_volume))
                //live: false

                rangeBackgroundColor: Style.colorTheme7
                rangeDistanceColor: sell_mode? Style.colorRed : Style.colorGreen

                second.value: parseFloat(non_null_volume)
                second.onValueChanged: { if(second.pressed) setVolume(General.formatDouble(second.value)) }
                secondTooltip.text: General.formatDouble(second.value, General.getRecommendedPrecision(to))

                first.value: parseFloat(API.app.trading_pg.min_trade_vol )
                first.onValueChanged: { if(first.pressed) setMinimumAmount(General.formatDouble(first.value)) }
                firstTooltip.text: General.formatDouble(first.value, General.getRecommendedPrecision(second.value))
            }


            // Fees


        }

        // Total amount

    }
}
