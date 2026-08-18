from sqlalchemy import Column, Integer, String, ForeignKey

from database import Base


class Product(Base):
    __tablename__ = "products"

    id = Column(Integer, primary_key=True)
    name = Column(String, nullable=False)
    price = Column(Integer, nullable=False)
    quantity = Column(Integer, nullable=False, default=0)


class Order(Base):
    __tablename__ = "orders"

    id = Column(String, primary_key=True)
    machine_id = Column(String, nullable=False)
    status = Column(String, nullable=False, default="pending")
    total = Column(Integer, nullable=False)


class OrderItem(Base):
    __tablename__ = "order_items"

    id = Column(Integer, primary_key=True, autoincrement=True)

    order_id = Column(
        String,
        ForeignKey("orders.id"),
        nullable=False
    )

    product_id = Column(
        Integer,
        ForeignKey("products.id"),
        nullable=False
    )

    quantity = Column(Integer, nullable=False)

    unit_price = Column(Integer, nullable=False)


class Payment(Base):
    __tablename__ = "payments"

    id = Column(Integer, primary_key=True, autoincrement=True)

    order_id = Column(
        String,
        ForeignKey("orders.id"),
        nullable=False
    )

    phone = Column(String, nullable=True)

    status = Column(
        String,
        nullable=False,
        default="pending"
    )