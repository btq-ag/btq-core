// Copyright (c) 2011-2020 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_QT_BTQADDRESSVALIDATOR_H
#define BTQ_QT_BTQADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class BTQAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit BTQAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** BTQ address widget validator, checks for a valid btq address.
 */
class BTQAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit BTQAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

#endif // BTQ_QT_BTQADDRESSVALIDATOR_H
