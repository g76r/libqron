/* Copyright 2014-2023 Hallowyn and others.
 * This file is part of qron, see <http://qron.eu/>.
 * Qron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Qron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public License
 * along with qron. If not, see <http://www.gnu.org/licenses/>.
 */
#include "accesscontrolconfig.h"
#include "auth/inmemoryauthenticator.h"
#include "auth/inmemoryusersdatabase.h"
#include "config/configutils.h"

static QSet<QString> excludedDescendantsForComments { "user-file", "user" };

class AccessControlConfigData : public QSharedData {
public:
  class UserFile {
  public:
    QString _path;
    InMemoryAuthenticator::Encoding _cipher;
    Utf8StringList _commentsList;
    explicit UserFile(QString path = QString(),
                      InMemoryAuthenticator::Encoding cipher
                      = InMemoryAuthenticator::Unknown)
      : _path(path), _cipher(cipher) { }
  };

  class User {
  public:
    QString _userId, _encodedPassword;
    InMemoryAuthenticator::Encoding _cipher;
    QSet<QString> _roles;
    Utf8StringList _commentsList;
    explicit User(QString userId = QString(),
                  QString encodedPassword = QString(),
                  InMemoryAuthenticator::Encoding cipher
                  = InMemoryAuthenticator::Unknown,
                  QSet<QString> roles = QSet<QString>())
      : _userId(userId), _encodedPassword(encodedPassword),
        _cipher(cipher), _roles(roles) { }
  };

  QList<UserFile> _userFiles;
  QList<User> _users;
  Utf8StringList _commentsList;
  AccessControlConfigData() {}
  AccessControlConfigData(ParamsProvider *context, PfNode node);
};

AccessControlConfig::AccessControlConfig() : d(new AccessControlConfigData) {
}

AccessControlConfig::AccessControlConfig(const AccessControlConfig &rhs)
  : d(rhs.d) {
}

AccessControlConfig::AccessControlConfig(ParamsProvider *context, PfNode node)
  : d(new AccessControlConfigData(context, node)) {
}

AccessControlConfig &AccessControlConfig::operator=(
    const AccessControlConfig &rhs) {
  if (this != &rhs)
    d.operator=(rhs.d);
  return *this;
}

AccessControlConfig::~AccessControlConfig() {
}

AccessControlConfigData::AccessControlConfigData(
    ParamsProvider *context, PfNode node) {
  foreach (PfNode child, node.childrenByName("user-file")) {
    QString path = child.contentAsUtf16().trimmed();
    InMemoryAuthenticator::Encoding cipher
        = InMemoryAuthenticator::encodingFromString(
            PercentEvaluator::eval_utf16(
              child.utf16attribute("cipher", "plain"), context));
    if (path.isEmpty())
      Log::error() << "access control user file with empty path: "
                   << child.toString();
    else if (cipher == InMemoryAuthenticator::Unknown)
      Log::error() << "access control user file '" << path
                   << "' with unsupported cipher type: '"
                   << InMemoryAuthenticator::encodingToString(cipher) << "'";
    else {
      UserFile userFile(path, cipher);
      ConfigUtils::loadComments(child, &userFile._commentsList);
      _userFiles.append(userFile);
    }
  }
  foreach (PfNode child, node.childrenByName("user"_ba)) {
    QString userId =
        PercentEvaluator::eval_utf16(child.contentAsUtf16(), context).trimmed();
    QString encodedPassword =
        PercentEvaluator::eval_utf16(child.utf16attribute("password"), context)
        .trimmed();
    InMemoryAuthenticator::Encoding cipher
        = InMemoryAuthenticator::encodingFromString(
            PercentEvaluator::eval_utf16(
              child.utf16attribute("cipher", "plain"), context).trimmed());
    auto rolelist = child.stringListAttribute("roles"_ba);
    auto roles = QSet<QString>(rolelist.begin(), rolelist.end());
    if (userId.isEmpty())
      Log::error() << "access control user with no id: " << child.toString();
    else if (encodedPassword.isEmpty())
      Log::error() << "access control user '" << userId
                   << "' with empty or no password";
    else {
      User user(userId, encodedPassword, cipher, roles);
      ConfigUtils::loadComments(child, &user._commentsList);
      _users.append(user);
    }
  }
  ConfigUtils::loadComments(node, &_commentsList,
                            excludedDescendantsForComments);
}


PfNode AccessControlConfig::toPfNode() const {
  PfNode node(QStringLiteral("access-control"));
  if (!d)
    return node;
  ConfigUtils::writeComments(&node, d->_commentsList);
  foreach (const AccessControlConfigData::UserFile &userFile, d->_userFiles) {
    PfNode child(QStringLiteral("user-file"), userFile._path);
    ConfigUtils::writeComments(&child, userFile._commentsList);
    child.appendChild(PfNode(QStringLiteral("cipher"), InMemoryAuthenticator
                             ::encodingToString(userFile._cipher)));
    node.appendChild(child);
  }
  foreach (const AccessControlConfigData::User &user, d->_users) {
    PfNode child(QStringLiteral("user"), user._userId);
    ConfigUtils::writeComments(&child, user._commentsList);
    child.appendChild(PfNode(QStringLiteral("password"),
                             user._encodedPassword));
    child.appendChild(PfNode(QStringLiteral("cipher"), InMemoryAuthenticator
                             ::encodingToString(user._cipher)));
    QStringList roles = user._roles.values();
    std::sort(roles.begin(), roles.end());
    child.appendChild(PfNode(QStringLiteral("roles"), roles.join(' ')));
    node.appendChild(child);
  }
  return node;
}

void AccessControlConfig::applyConfiguration(
    InMemoryAuthenticator *authenticator,
    InMemoryUsersDatabase *usersDatabase,
    QFileSystemWatcher *accessControlFilesWatcher) const {
  if (authenticator)
    authenticator->clearUsers();
  if (usersDatabase)
    usersDatabase->clearUsers();
  if (accessControlFilesWatcher) {
    QStringList files = accessControlFilesWatcher->files();
    if (!files.isEmpty())
      accessControlFilesWatcher->removePaths(files);
  }
  if (!d)
    return;
  foreach (const AccessControlConfigData::UserFile &userFile, d->_userFiles) {
    QString path = userFile._path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      Log::error() << "cannot open access control user file '" << path
                   << "': " << file.errorString();
      continue;
    }
    if (accessControlFilesWatcher)
      accessControlFilesWatcher->addPath(path);
    QByteArray row;
    while (row = file.readLine(65535), !row.isNull()) {
      // implicitly, format is: login:crypted_password:role1,role2,rolen
      // later, other formats may be supported
      QString line = QString::fromUtf8(row).trimmed();
      if (line.size() == 0 || line.startsWith('#'))
        continue; // ignore empty lines and support # as a comment mark
      QStringList fields = line.split(':');
      if (fields.size() < 3) {
        Log::error() << "access control user file '" << path
                     << "' contains invalid line: " << line;
        continue;
      }
      QString id = fields[0].trimmed();
      QString password = fields[1].trimmed();
      QSet<QString> roles;
      foreach (const QString role,
               fields[2].trimmed().split(',', Qt::SkipEmptyParts))
        roles.insert(role.trimmed());
      if (id.isEmpty() || password.isEmpty() || roles.isEmpty()) {
        Log::error() << "access control user file '" << path
                     << "' contains a line with empty mandatory fields: "
                     << line;
        continue;
      }
      if (authenticator)
        authenticator->insertUser(id, password, userFile._cipher);
      if (usersDatabase)
        usersDatabase->insertUser(id, roles);
    }
    if (file.error() != QFileDevice::NoError)
      Log::error() << "error reading access control user file '" << path
                   << "': " << file.errorString();
  }
  foreach (const AccessControlConfigData::User &user, d->_users) {
    if (authenticator)
      authenticator->insertUser(user._userId, user._encodedPassword,
                                user._cipher);
    if (usersDatabase)
      usersDatabase->insertUser(user._userId, user._roles);
  }
}

bool AccessControlConfig::isEmpty() const {
  return !d || (d->_userFiles.isEmpty() && d->_users.isEmpty());
}
